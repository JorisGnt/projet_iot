
#include <stdio.h>
#include <math.h>
#include "prediction.h"

/*
 * Réseau neuronal :
 * 40 entrées -> 16 neurones -> 8 neurones -> 2 sorties
 *
 * Entrées :
 * 20 températures
 * 20 humidités
 *
 * Sorties :
 * variation température dans 5 min
 * variation humidité dans 5 min
 */

/* Fonction ReLU */
static float relu(float x)
{
    return (x > 0.0f) ? x : 0.0f;
}

/* Saturation des entrées */
static float clamp(float x, float min, float max)
{
    if (x < min)
        return min;
    if (x > max)
        return max;
    return x;
}

/*
 * Fonction principale de prédiction.
 *
 * T[20]  : températures des 10 dernières minutes
 * RH[20] : humidités des 10 dernières minutes
 *
 * dT     : variation de température prédite
 * dRH    : variation d'humidité prédite
 */
void AI_prediction(
    const float T[FENETRE],
    const float RH[FENETRE],
    float *dT,
    float *dRH)
{
    float entree[N_ENTREES];

    float couche1[N_C1];
    float couche2[N_C2];

    float sortie[2];

    int i, j;

    /* 1. Normalisation des températures */
    for (i = 0; i < FENETRE; i++)
    {

        float valeur = clamp(T[i], T_BAS, T_HAUT);

        entree[i] = (valeur - MOY_T) / ET_T;
    }

    /* 2. Normalisation des humidités */
    for (i = 0; i < FENETRE; i++)
    {

        float valeur = clamp(RH[i], RH_BAS, RH_HAUT);

        entree[FENETRE + i] =
            (valeur - MOY_RH) / ET_RH;
    }

    /* 3. Première couche : 40 -> 16 */
    for (j = 0; j < N_C1; j++)
    {

        float somme = B1[j];

        for (i = 0; i < N_ENTREES; i++)
        {
            somme += entree[i] * W1[i * N_C1 + j];
        }

        couche1[j] = relu(somme);
    }

    /* 4. Deuxième couche : 16 -> 8 */
    for (j = 0; j < N_C2; j++)
    {

        float somme = B2[j];

        for (i = 0; i < N_C1; i++)
        {
            somme += couche1[i] * W2[i * N_C2 + j];
        }

        couche2[j] = relu(somme);
    }

    /* 5. Couche de sortie : 8 -> 2 */
    for (j = 0; j < 2; j++)
    {

        float somme = B3[j];

        for (i = 0; i < N_C2; i++)
        {
            somme += couche2[i] * W3[i * 2 + j];
        }

        sortie[j] = somme;
    }

    /* 6. Dénormalisation des sorties */

    *dT = sortie[0] * ET_DT + MOY_DT;

    *dRH = sortie[1] * ET_DRH + MOY_DRH;
}
