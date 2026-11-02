import csv
import os
import time
import threading
from queue import Queue


class CSVSaver(threading.Thread):
    def __init__(self, filename="donnees_capteurs.csv", interval=300):
        super().__init__()
        self.filename = filename
        self.interval = interval  # 300 secondes = 5 minutes
        self.buffer = Queue()
        self.running = True
        self.daemon = True

        self._init_csv()

    def _init_csv(self):
        """Crée le fichier CSV avec son en-tête s'il n'existe pas encore."""
        if not os.path.exists(self.filename):
            with open(self.filename, mode="w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow(
                    [
                        "timestamp_s",
                        "temperature_c",
                        "humidite_pct",
                        "adc_pct",
                        "alerte_porte",
                        "alerte_temp",
                    ]
                )

    def add_record(self, timestamp, temp, hum, adc_pct, alert_door, alert_temp):
        """Empile une mesure dans le tampon à sauvegarder."""
        self.buffer.put((timestamp, temp, hum, adc_pct, alert_door, alert_temp))

    def run(self):
        print(
            f"[*] Module CSV actif : écriture toutes les {self.interval // 60} min dans '{self.filename}'."
        )
        while self.running:
            # Attente de l'intervalle avec vérification régulière de self.running
            for _ in range(int(self.interval)):
                if not self.running:
                    break
                time.sleep(1)

            self.flush_to_csv()

    def flush_to_csv(self):
        """Écrit toutes les données accumulées dans le fichier CSV."""
        if self.buffer.empty():
            return

        rows_to_write = []
        while not self.buffer.empty():
            rows_to_write.append(self.buffer.get_nowait())

        try:
            with open(self.filename, mode="a", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                for row in rows_to_write:
                    writer.writerow(
                        [
                            round(row[0], 2),  # Temps relatif (s)
                            row[1],  # Température
                            row[2],  # Humidité
                            round(row[3], 2),  # ADC converti en %
                            row[4],  # Alerte porte
                            row[5],  # Alerte température
                        ]
                    )
            print(
                f"[CSV] {len(rows_to_write)} mesures sauvegardées dans '{self.filename}'."
            )
        except Exception as e:
            print(f"[-] Erreur lors de l'écriture CSV : {e}")

    def stop(self):
        self.running = False
        self.flush_to_csv()  # Sauvegarde les dernières mesures avant la fermeture
