"""
Entraînement du réseau de neurones « panne » + export en C.

Étapes :
  1. Regroupe les mesures (1 toutes les 5 s) en blocs de 30 s, comme le capteur.
  2. Fabrique les exemples : 10 dernières minutes -> variation de T et d'humidité dans 5 min.
  3. Entraîne un petit MLP sur le fonctionnement normal seulement.
  4. Calcule le seuil d'alerte sur la validation (normal aussi).
  5. Exporte les poids dans modele_poids.h pour le code C.

Usage : python3 entrainement.py donnees_frigo_simulees.csv
"""
import sys, numpy as np, pandas as pd
from sklearn.neural_network import MLPRegressor

# ---- Réglages (les mêmes que dans le code C) ----
ECH_PAR_BLOC = 6        # 6 mesures de 5 s = 1 bloc de 30 s
FENETRE      = 20       # 20 blocs = 10 min de passé
HORIZON      = 10       # prédire 10 blocs plus loin = 5 min
CACHEES      = (16, 8)  # 2 couches cachées, petites pour tenir dans le capteur
T_MIN, T_MAX = -30.0, 40.0   # en dehors : mesure aberrante, ignorée
FIN_ENTRAIN  = 126      # minutes : 30 -> 126 = 80 % du normal
FIN_NORMAL   = 150      # minutes : 126 -> 150 = 20 % pour la validation
CERTITUDE    = 0.95     # niveau de confiance de la fourchette de prédiction

def blocs(g):
    """Reproduit exactement ce que fait le capteur : moyenne sur 30 s."""
    out, acc, derniere_T = [], [], None
    for row in g.itertuples(index=False):
        T = row.temperature_C
        if not (T_MIN <= T <= T_MAX):          # valeur aberrante -> on garde la précédente
            T = derniere_T if derniere_T is not None else 4.0
        derniere_T = T
        acc.append((T, row.humidite_pct, row.horodatage, row.phase))
        if len(acc) == ECH_PAR_BLOC:
            a = np.array([x[:2] for x in acc], dtype=float)
            out.append(dict(T=a[:,0].mean(), RH=a[:,1].mean(),
                            t=acc[-1][2], phase=acc[-1][3]))

            acc = []
    b = pd.DataFrame(out)
    b["minute"] = (b.t - g.horodatage.iloc[0]).dt.total_seconds() / 60
    return b

def exemples(b):
    """X = 20 T puis 20 RH (du plus ancien au plus récent),
    y = [T(t+5 min) - T(t), RH(t+5 min) - RH(t)]."""
    X, y, fin = [], [], []
    T, RH = b["T"].values, b.RH.values
    for i in range(FENETRE - 1, len(b) - HORIZON):
        X.append(np.concatenate([T[i-FENETRE+1:i+1], RH[i-FENETRE+1:i+1]]))
        y.append([T[i+HORIZON] - T[i], RH[i+HORIZON] - RH[i]])
        fin.append(i)
    return np.array(X), np.array(y), np.array(fin)

df = pd.read_csv(sys.argv[1] if len(sys.argv) > 1 else "donnees_frigo_simulees.csv",
                 parse_dates=["horodatage"])

Xtr, ytr, Xva, yva = [], [], [], []
for f, g in df.groupby("frigo"):
    b = blocs(g.reset_index(drop=True))
    X, y, fin = exemples(b)
    debut = b.minute.values[fin - FENETRE + 1]          # début de la fenêtre
    cible = b.minute.values[fin + HORIZON]              # instant prédit
    tr = (debut >= 30) & (cible < FIN_ENTRAIN)          # tout dans la partie entraînement
    va = (debut >= FIN_ENTRAIN) & (cible < FIN_NORMAL)  # tout dans la validation
    # La validation est courte (24 min) : on autorise des fenêtres qui commencent
    # avant, tant que l'instant prédit est dans la validation et jamais vu.
    va = (cible >= FIN_ENTRAIN + 3) & (cible < FIN_NORMAL) & (debut >= 30)
    Xtr.append(X[tr]); ytr.append(y[tr]); Xva.append(X[va]); yva.append(y[va])
Xtr, ytr, Xva, yva = map(np.concatenate, (Xtr, ytr, Xva, yva))

# ---- Normalisation (calculée sur l'entraînement seulement) ----
moy_T, et_T   = Xtr[:, :FENETRE].mean(), Xtr[:, :FENETRE].std()
moy_RH, et_RH = Xtr[:, FENETRE:].mean(), Xtr[:, FENETRE:].std()
T_BAS, T_HAUT   = Xtr[:, :FENETRE].min(), Xtr[:, :FENETRE].max()
RH_BAS, RH_HAUT = Xtr[:, FENETRE:].min(), Xtr[:, FENETRE:].max()
def norm(X):
    Z = X.copy()
    # saturation : on borne les entrées à ce que le réseau a vu à l'entraînement
    Z[:, :FENETRE] = np.clip(Z[:, :FENETRE], T_BAS, T_HAUT)
    Z[:, FENETRE:] = np.clip(Z[:, FENETRE:], RH_BAS, RH_HAUT)
    Z[:, :FENETRE] = (Z[:, :FENETRE] - moy_T) / et_T
    Z[:, FENETRE:] = (Z[:, FENETRE:] - moy_RH) / et_RH
    return Z
moy_y, et_y = ytr.mean(axis=0), ytr.std(axis=0)   # une moyenne par sortie

mlp = MLPRegressor(hidden_layer_sizes=CACHEES, activation="relu", alpha=1e-3,
                   learning_rate_init=1e-3, max_iter=3000, early_stopping=True,
                   random_state=0)
mlp.fit(norm(Xtr), (ytr - moy_y) / et_y)

def predit(X):
    return mlp.predict(norm(X)) * et_y + moy_y

# ---- Seuil sur la validation ----
# résidu = valeur mesurée - valeur prédite 5 min plus tôt
# (positif = plus chaud / plus humide que prévu, ce qu'on cherche pour une panne)
res_va = yva - predit(Xva)
res_tr = ytr - predit(Xtr)
res_max = np.maximum(res_va.max(axis=0), res_tr.max(axis=0))
SEUIL_T, SEUIL_RH = 1.5 * res_max
print(f"Exemples : {len(Xtr)} entraînement, {len(Xva)} validation")
print(f"Erreur moyenne |résidu| validation : T {np.abs(res_va[:,0]).mean():.3f} °C, "
      f"humidité {np.abs(res_va[:,1]).mean():.3f} %")
print(f"Seuils : T {SEUIL_T:.3f} °C, humidité {SEUIL_RH:.3f} %")

# ---- Intervalle de confiance ----
# Marge telle que CERTITUDE % des erreurs de validation (données jamais vues) sont en dessous.
# Prédiction affichée : valeur prédite ± marge.
MARGE_T, MARGE_RH = np.quantile(np.abs(res_va), CERTITUDE, axis=0)
print(f"Intervalle à {CERTITUDE:.0%} : T ± {MARGE_T:.3f} °C, humidité ± {MARGE_RH:.3f} %")

nb_params = sum(w.size for w in mlp.coefs_) + sum(b.size for b in mlp.intercepts_)
print(f"Paramètres : {nb_params} ({nb_params*4/1024:.1f} Ko en float)")

# ---- Export C ----
def tab(nom, a):
    v = ", ".join(f"{x:.8e}f" for x in np.asarray(a).ravel())
    return f"static const float {nom}[{np.asarray(a).size}] = {{ {v} }};\n"

W1, W2, W3 = mlp.coefs_          # formes : (40,16), (16,8), (8,2)
B1, B2, B3 = mlp.intercepts_
with open("modele_poids.h", "w") as h:
    h.write("/* Généré par entrainement.py -- ne pas modifier à la main */\n")
    h.write("#ifndef MODELE_POIDS_H\n#define MODELE_POIDS_H\n\n")
    h.write(f"#define N_ENTREES {2*FENETRE}\n#define N_C1 {CACHEES[0]}\n#define N_C2 {CACHEES[1]}\n\n")
    h.write(f"#define MOY_T  {moy_T:.8f}f\n#define ET_T   {et_T:.8f}f\n")
    h.write(f"#define MOY_RH {moy_RH:.8f}f\n#define ET_RH  {et_RH:.8f}f\n")
    h.write(f"#define T_BAS  {T_BAS:.8f}f\n#define T_HAUT  {T_HAUT:.8f}f\n")
    h.write(f"#define RH_BAS {RH_BAS:.8f}f\n#define RH_HAUT {RH_HAUT:.8f}f\n")
    h.write(f"#define MOY_DT  {moy_y[0]:.8f}f\n#define ET_DT   {et_y[0]:.8f}f\n")
    h.write(f"#define MOY_DRH {moy_y[1]:.8f}f\n#define ET_DRH  {et_y[1]:.8f}f\n")
    h.write(f"#define SEUIL_T  {SEUIL_T:.6f}f  /* °C, calculé sur la validation */\n")
    h.write(f"#define SEUIL_RH {SEUIL_RH:.6f}f  /* %,  calculé sur la validation */\n")
    h.write(f"#define MARGE_T  {MARGE_T:.6f}f  /* °C, intervalle à {CERTITUDE:.0%} */\n")
    h.write(f"#define MARGE_RH {MARGE_RH:.6f}f  /* %,  intervalle à {CERTITUDE:.0%} */\n\n")
    h.write("/* Poids rangés [entrée][neurone] */\n")
    for nom, a in [("W1", W1), ("B1", B1), ("W2", W2), ("B2", B2), ("W3", W3), ("B3", B3)]:
        h.write(tab(nom, a))
    h.write("\n#endif\n")
print("Écrit : modele_poids.h")

