import time
from csv_saver import CSVSaver
from data_manager import DataManager
from tcp_receiver import TCPReceiver

# 1. Créer et démarrer l'enregistreur CSV (300 sec = 5 min)
csv_saver = CSVSaver(filename="donnees_capteurs.csv", interval=300)
csv_saver.start()

# 2. Transmettre l'instance au DataManager
data_manager = DataManager(csv_saver=csv_saver)

# 3. Lancer le récepteur TCP
receiver = TCPReceiver("0.0.0.0", 8002, data_manager)
receiver.start()

# 4. Maintenir le programme principal en vie
try:
    print("[*] Système en cours d'exécution. Appuyez sur Ctrl+C pour arrêter.")
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\n[*] Arrêt du système demandé...")
    # Arrête proprement les threads avant de quitter
    csv_saver.stop()
    receiver.stop()
    print("[*] Système arrêté proprement.")
