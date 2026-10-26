from collections import deque
import time


class DataManager:
    ADC_BIT = 12

    def __init__(self, max_points=300):
        # max_points = 300 signifie qu'on garde seulement les 300 dernières secondes (si 1 valeur/sec)
        self.times = deque(maxlen=max_points)
        self.temps = deque(maxlen=max_points)
        self.hums = deque(maxlen=max_points)
        self.adcs = deque(maxlen=max_points)
        self.alert_door_status = deque(maxlen=max_points)
        self.alert_temp_status = deque(maxlen=max_points)

        self.start_time = time.time()

    def add_data(self, temp, hum, adc, alert_door_status, alert_temp_status):
        current_time = time.time() - self.start_time

        self.times.append(current_time)
        self.temps.append(temp)
        self.hums.append(hum)
        self.adcs.append(100 * (2**self.ADC_BIT - adc) / 2**self.ADC_BIT)
        self.alert_door_status.append(alert_door_status)
        self.alert_temp_status.append(alert_temp_status)
