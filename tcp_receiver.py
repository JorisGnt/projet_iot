import socket
import json
import threading


class TCPReceiver(threading.Thread):
    def __init__(self, host, port, data_manager):
        super().__init__()
        self.host = host
        self.port = port
        self.data_manager = data_manager
        self.running = True
        self.daemon = True  # S'arrête tout seul quand on ferme le dashboard

    def run(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((self.host, self.port))
            s.listen()
            print(f"[*] Serveur TCP en arrière-plan sur le port {self.port}...")

            while self.running:
                try:
                    s.settimeout(1.0)
                    conn, addr = s.accept()

                    with conn:
                        print(f"[+] Carte STM32 connectée : {addr}")
                        conn.settimeout(None)
                        buffer = ""

                        while self.running:
                            data = conn.recv(1024)
                            if not data:
                                print("[-] La carte a fermé la connexion.")
                                break

                            buffer += data.decode("utf-8", errors="ignore")

                            while "\n" in buffer:
                                line, buffer = buffer.split("\n", 1)
                                line = line.strip()
                                if line:
                                    self._parse_and_store(line)

                except socket.timeout:
                    continue
                except Exception as e:
                    print(f"[-] Erreur réseau : {e}")

    def _parse_and_store(self, json_str):
        try:
            print(f"[REÇU] {json_str}")
            data = json.loads(json_str)
            self.data_manager.add_data(
                temp=data.get("temp", 0.0),
                hum=data.get("hum", 0.0),
                adc=data.get("adc", 0),
                alert_door_status=data.get("alert_door", 0),
                alert_temp_status=data.get("alert_temp", 0),
            )
        except json.JSONDecodeError:
            pass  # Ignore les messages qui ne sont pas du JSON

    def stop(self):
        self.running = False
