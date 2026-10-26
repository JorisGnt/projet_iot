import sys
from datetime import datetime
import pyqtgraph as pg
from PyQt6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QLabel,
    QFrame,
    QScrollArea,
    QSizePolicy,
)
from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QFont

from data_manager import DataManager
from tcp_receiver import TCPReceiver


class AlertWidget(QFrame):
    """Widget personnalisé représentant une alerte active (Bannière colorée)."""

    def __init__(self, title, bg_color):
        super().__init__()
        self.setStyleSheet(f"""
            QFrame {{
                background-color: {bg_color};
                border-radius: 8px;
                margin-bottom: 10px;
            }}
        """)
        self.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Fixed)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)

        # Titre de l'alerte
        self.lbl_title = QLabel(title)
        self.lbl_title.setFont(QFont("Segoe UI", 12, QFont.Weight.Bold))
        self.lbl_title.setStyleSheet(
            "color: white; border: none; background: transparent;"
        )

        # Heure de début
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.lbl_time = QLabel(f"Actif depuis : {timestamp}")
        self.lbl_time.setFont(QFont("Segoe UI", 10))
        self.lbl_time.setStyleSheet(
            "color: rgba(255, 255, 255, 0.8); border: none; background: transparent;"
        )

        layout.addWidget(self.lbl_title)
        layout.addWidget(self.lbl_time)


class DashboardApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Supervision Chambre Froide")
        self.resize(1400, 850)

        pg.setConfigOption("background", "#242526")
        pg.setConfigOption("foreground", "#b0b3b8")
        pg.setConfigOptions(antialias=True)

        self.data_manager = DataManager(max_points=60)

        # Variables pour stocker les widgets d'alerte actuellement affichés
        self.active_door_alert = None
        self.active_temp_alert = None

        self.last_door_state = 0
        self.last_temp_state = 0

        self._build_ui()

        self.tcp_server = TCPReceiver("0.0.0.0", 8002, self.data_manager)
        self.tcp_server.start()

        self.timer = QTimer()
        self.timer.timeout.connect(self.update_dashboard)
        self.timer.start(100)

    def _create_card_frame(self):
        frame = QFrame()
        frame.setStyleSheet("""
            QFrame {
                background-color: #242526;
                border-radius: 12px;
                border: 1px solid #3a3b3c;
            }
        """)
        return frame

    def _build_ui(self):
        central_widget = QWidget()
        central_widget.setStyleSheet("background-color: #18191a;")
        self.setCentralWidget(central_widget)

        main_layout = QHBoxLayout(central_widget)
        main_layout.setContentsMargins(20, 20, 20, 20)
        main_layout.setSpacing(20)

        left_card = self._create_card_frame()
        left_layout = QVBoxLayout(left_card)
        left_layout.setContentsMargins(15, 15, 15, 15)

        lbl_graphs = QLabel("ÉVOLUTION EN TEMPS RÉEL")
        lbl_graphs.setFont(QFont("Segoe UI", 12, QFont.Weight.Bold))
        lbl_graphs.setStyleSheet(
            "color: #e4e6eb; background: transparent; border: none;"
        )
        left_layout.addWidget(lbl_graphs)

        self.plot_temp = pg.PlotWidget(title="Température (°C)")
        self.plot_hum = pg.PlotWidget(title="Humidité (%)")
        self.plot_adc = pg.PlotWidget(title="Capteur Luminosité (%)")

        for p in [self.plot_temp, self.plot_hum, self.plot_adc]:
            p.showGrid(x=True, y=True, alpha=0.3)
            p.getAxis("left").setPen("#555")
            p.getAxis("bottom").setPen("#555")
            p.setStyleSheet("border: none;")
            left_layout.addWidget(p)

        self.curve_temp = self.plot_temp.plot(pen=pg.mkPen(color="#ff4757", width=3))
        self.curve_hum = self.plot_hum.plot(pen=pg.mkPen(color="#1e90ff", width=3))
        self.curve_adc = self.plot_adc.plot(pen=pg.mkPen(color="#2ed573", width=3))

        main_layout.addWidget(left_card, stretch=7)

        right_layout = QVBoxLayout()
        right_layout.setSpacing(20)

        kpi_card = self._create_card_frame()
        kpi_layout = QVBoxLayout(kpi_card)
        kpi_layout.setContentsMargins(15, 15, 15, 15)

        lbl_kpi_title = QLabel("VALEURS INSTANTANÉES")
        lbl_kpi_title.setFont(QFont("Segoe UI", 12, QFont.Weight.Bold))
        lbl_kpi_title.setStyleSheet(
            "color: #e4e6eb; background: transparent; border: none;"
        )
        kpi_layout.addWidget(lbl_kpi_title)

        kpi_grid = QGridLayout()
        self.lbl_temp = self._create_kpi_label("#ff4757")
        self.lbl_hum = self._create_kpi_label("#1e90ff")
        self.lbl_adc = self._create_kpi_label("#2ed573")

        kpi_grid.addWidget(self._create_subtitle_label("Température"), 0, 0)
        kpi_grid.addWidget(self.lbl_temp, 1, 0)
        kpi_grid.addWidget(self._create_subtitle_label("Humidité"), 0, 1)
        kpi_grid.addWidget(self.lbl_hum, 1, 1)
        kpi_grid.addWidget(self._create_subtitle_label("Luminosité (%)"), 2, 0, 1, 2)
        kpi_grid.addWidget(self.lbl_adc, 3, 0, 1, 2)

        kpi_layout.addLayout(kpi_grid)
        right_layout.addWidget(kpi_card, stretch=2)

        # --- Carte des Alertes Dynamiques ---
        alert_card = self._create_card_frame()
        alert_layout = QVBoxLayout(alert_card)
        alert_layout.setContentsMargins(15, 15, 15, 15)

        lbl_alert_title = QLabel("ALERTES ACTIVES")
        lbl_alert_title.setFont(QFont("Segoe UI", 12, QFont.Weight.Bold))
        lbl_alert_title.setStyleSheet(
            "color: #e4e6eb; background: transparent; border: none;"
        )
        alert_layout.addWidget(lbl_alert_title)

        scroll_area = QScrollArea()
        scroll_area.setWidgetResizable(True)
        scroll_area.setStyleSheet("border: none; background: transparent;")

        # Conteneur interne où les alertes seront ajoutées et retirées
        self.alerts_container = QWidget()
        self.alerts_container.setStyleSheet("background: transparent;")
        self.alerts_layout = QVBoxLayout(self.alerts_container)
        self.alerts_layout.setAlignment(
            Qt.AlignmentFlag.AlignTop
        )  # Aligne les alertes en haut
        self.alerts_layout.setContentsMargins(0, 0, 0, 0)

        scroll_area.setWidget(self.alerts_container)
        alert_layout.addWidget(scroll_area)

        # Message par défaut quand tout va bien
        self.lbl_all_good = QLabel("✅ Aucun problème détecté.")
        self.lbl_all_good.setFont(QFont("Segoe UI", 10, QFont.Weight.Bold))
        self.lbl_all_good.setStyleSheet(
            "color: #2ed573; background: transparent; border: none;"
        )
        self.lbl_all_good.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.alerts_layout.addWidget(self.lbl_all_good)

        right_layout.addWidget(alert_card, stretch=5)
        main_layout.addLayout(right_layout, stretch=3)

    def _create_kpi_label(self, color):
        lbl = QLabel("--")
        lbl.setFont(QFont("Segoe UI", 28, QFont.Weight.Bold))
        lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lbl.setStyleSheet(f"color: {color}; background: transparent; border: none;")
        return lbl

    def _create_subtitle_label(self, text):
        lbl = QLabel(text)
        lbl.setFont(QFont("Segoe UI", 10))
        lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lbl.setStyleSheet("color: #b0b3b8; background: transparent; border: none;")
        return lbl

    def update_dashboard(self):
        if len(self.data_manager.times) > 0:
            times = list(self.data_manager.times)
            temps = list(self.data_manager.temps)
            hums = list(self.data_manager.hums)
            adcs = list(self.data_manager.adcs)

            self.curve_temp.setData(times, temps)
            self.curve_hum.setData(times, hums)
            self.curve_adc.setData(times, adcs)

            self.lbl_temp.setText(f"{temps[-1]:.1f} °C")
            self.lbl_hum.setText(f"{hums[-1]:.1f} %")
            self.lbl_adc.setText(f"{adcs[-1]:.0f}")

            if (
                hasattr(self.data_manager, "alert_door_status")
                and len(self.data_manager.alert_door_status) > 0
            ):
                current_door = self.data_manager.alert_door_status[-1]
                if current_door != self.last_door_state:
                    if current_door == 1:
                        # La porte s'ouvre : on crée et on affiche le widget Rouge
                        self.active_door_alert = AlertWidget(
                            "ALERTE : Porte Ouverte", "#ff4757"
                        )
                        self.alerts_layout.addWidget(self.active_door_alert)
                    else:
                        # La porte se ferme : on détruit le widget
                        if self.active_door_alert:
                            self.alerts_layout.removeWidget(self.active_door_alert)
                            self.active_door_alert.deleteLater()
                            self.active_door_alert = None
                    self.last_door_state = current_door

            if (
                hasattr(self.data_manager, "alert_temp_status")
                and len(self.data_manager.alert_temp_status) > 0
            ):
                current_temp = self.data_manager.alert_temp_status[-1]
                if current_temp != self.last_temp_state:

                    # On retire l'ancien widget s'il y en avait un (pour éviter les doublons si on passe de 1 à 2)
                    if self.active_temp_alert:
                        self.alerts_layout.removeWidget(self.active_temp_alert)
                        self.active_temp_alert.deleteLater()
                        self.active_temp_alert = None

                    # On crée le nouveau widget selon le niveau d'alerte
                    if current_temp == 1:
                        self.active_temp_alert = AlertWidget(
                            "PRÉVENTIF : Dérive Thermique (IA)", "#e1b12c"
                        )
                        self.alerts_layout.addWidget(self.active_temp_alert)
                    elif current_temp >= 2:
                        self.active_temp_alert = AlertWidget(
                            "CRITIQUE : Température hors seuils", "#ff4757"
                        )
                        self.alerts_layout.addWidget(self.active_temp_alert)

                    self.last_temp_state = current_temp

            # Cacher ou montrer le message "Aucun problème"
            if self.active_door_alert is None and self.active_temp_alert is None:
                self.lbl_all_good.show()
            else:
                self.lbl_all_good.hide()

    def closeEvent(self, event):
        print("[*] Arrêt du serveur TCP et fermeture...")
        self.tcp_server.stop()
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = DashboardApp()
    window.show()
    sys.exit(app.exec())
