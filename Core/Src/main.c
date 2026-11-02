/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body - Intégration IA et Alertes
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32l475e_iot01.h"
#include "stm32l475e_iot01_tsensor.h"
#include "stm32l475e_iot01_hsensor.h"
#include "wifi.h"
#include "prediction.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* --- Constantes pour le modèle IA --- */
#define FENETRE 20            /* Nombre de mesures pour l'historique (40 entrées / 2) */
#define AI_HORIZON_MIN 5      /* Prédiction à +5 minutes */
#define AI_SAMPLE_PERIOD_S 30 /* Une mesure ajoutée toutes les 30 secondes */

/* Historique glissant des mesures : [0] = la plus ancienne, [FENETRE-1] = la plus récente */
typedef struct
{
  float temp[FENETRE];
  float hum[FENETRE];
  uint8_t count; /* Nombre de mesures valides (plafonné à FENETRE) */
} History_t;

/* Résultat de la prédiction IA à +AI_HORIZON_MIN minutes */
typedef struct
{
  bool valid; /* false tant que l'historique n'est pas complet */
  float temp; /* Température prédite [°C] */
  float hum;  /* Humidité prédite [%]     */
} Prediction_t;

/* État mémorisé des alertes (chaque alerte a son hystérésis et temporisation) */
typedef struct
{
  bool door_open;          /* ADC au-dessus du seuil porte depuis > ADC_DOOR_DELAY_MS */
  bool door_timer_running; /* Minuteur de temporisation porte actif                   */
  uint32_t door_start_ms;  /* Horodatage du début du dépassement de seuil porte       */
  bool temp_predicted;     /* Température prédite au-dessus du seuil               */
  bool temp_critical;      /* Température actuelle au-dessus du seuil              */
} AlertState_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* --- Configuration Wi-Fi --- */
#define WIFI_SSID "joris"
#define WIFI_PASSWORD "jorisgenot"

/* --- Configuration Serveur TCP --- */
#define SERVER_IP_1 10
#define SERVER_IP_2 205
#define SERVER_IP_3 147
#define SERVER_IP_4 212
#define SERVER_PORT 8002

#define WIFI_WRITE_TIMEOUT 10000
#define WIFI_READ_TIMEOUT 10000
#define CONNECTION_TRIAL_MAX 10

/* --- Seuils d'alerte --- */
#define TEMP_THRESHOLD_C 8.0f
#define TEMP_HYSTERESIS_C 1.0f /* L'alerte retombe sous (seuil - hystérésis) */

/* Porte : alerte quand l'ADC dépasse le seuil (12 bits : 0..4095) */
#define ADC_DOOR_THRESHOLD 2000.0f
#define ADC_DOOR_HYSTERESIS 100.0f
#define ADC_DOOR_DELAY_MS 20000U /* Temps de maintien au-dessus du seuil pour activer l'alerte (ms) */

/* --- Alarme sonore et lumineuse (buzzer actif externe sur D5 + LED2, port B) --- */
#define ALARM_GPIO_PORT GPIOB
#define ALARM_PINS (LED2_Pin | ARD_D5_Pin)

/* --- Cadencement --- */
#define LOOP_PERIOD_MS 1000U                          /* Mesure, affichage et envoi */
#define SAMPLE_PERIOD_MS (AI_SAMPLE_PERIOD_S * 1000U) /* Pas de l'historique IA    */

/* --- Formatage --- */
#define TX_BUFFER_SIZE 128U
#define FLOAT_STR_LEN 16U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DFSDM_Channel_HandleTypeDef hdfsdm1_channel1;
I2C_HandleTypeDef hi2c2;
QSPI_HandleTypeDef hqspi;
SPI_HandleTypeDef hspi3;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;
PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */
uint8_t ServerIP[] = {SERVER_IP_1, SERVER_IP_2, SERVER_IP_3, SERVER_IP_4};
uint32_t TcpSocket = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DFSDM1_Init(void);
static void MX_I2C2_Init(void);
static void MX_QUADSPI_Init(void);
static void MX_SPI3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
static void MX_ADC1_Init(void);

/* USER CODE BEGIN PFP */
/* Prototype du modèle IA généré */
extern void AI_prediction(const float T[], const float RH[], float *dT, float *dRH);

/* Réseau */
bool Connect_To_WiFi(void);
bool Connect_To_TCP_Server(void);
static void Send_Telemetry(float temperature, float humidity, uint32_t adc_val,
                           uint8_t alert_door, uint8_t alert_temp);

/* Mesures et Historique */
static uint32_t Read_ADC(uint32_t previous_value);
static void History_Push(History_t *h, float temp, float hum);
static Prediction_t AI_Predict(const History_t *h);

/* Alertes centralisées */
static bool Above_Threshold(bool previous, float value, float threshold, float hysteresis);
static void Manage_Alerts(AlertState_t *state, float current_temp, uint32_t adc_val,
                          const Prediction_t *pred, uint8_t *alert_door, uint8_t *alert_temp);

/* Utilitaire console */
static void Format_Float(char *buf, size_t size, float value);
static void Log_Status(float temperature, float humidity, uint32_t adc_val,
                       uint8_t alert_door, uint8_t alert_temp,
                       const History_t *history, const Prediction_t *prediction);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ==========================================================================
 *  RÉSEAU
 * ========================================================================== */

bool Connect_To_WiFi(void)
{
  uint8_t IP_Addr[4] = {0};
  uint8_t MAC_Addr[6] = {0};

  printf("\r\n--- Initialisation du module Wi-Fi ---\r\n");
  if (WIFI_Init() != WIFI_STATUS_OK)
  {
    printf("[ERREUR] Impossible d'initialiser le module Wi-Fi.\r\n");
    return false;
  }
  printf("[OK] Module Wi-Fi initialise.\r\n");

  if (WIFI_GetMAC_Address(MAC_Addr, sizeof(MAC_Addr)) == WIFI_STATUS_OK)
  {
    printf("> MAC Address : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           MAC_Addr[0], MAC_Addr[1], MAC_Addr[2], MAC_Addr[3], MAC_Addr[4], MAC_Addr[5]);
  }

  printf("> Connexion au reseau Wi-Fi '%s'...\r\n", WIFI_SSID);
  if (WIFI_Connect(WIFI_SSID, WIFI_PASSWORD, WIFI_ECN_WPA2_PSK) == WIFI_STATUS_OK)
  {
    if (WIFI_GetIP_Address(IP_Addr, sizeof(IP_Addr)) == WIFI_STATUS_OK)
    {
      printf("[OK] Connecte ! IP de la carte : %d.%d.%d.%d\r\n",
             IP_Addr[0], IP_Addr[1], IP_Addr[2], IP_Addr[3]);
      return true;
    }
  }

  printf("[ERREUR] Echec de la connexion Wi-Fi.\r\n");
  return false;
}

bool Connect_To_TCP_Server(void)
{
  int trials = CONNECTION_TRIAL_MAX;
  printf("\r\n> Tentative de connexion au Serveur TCP %d.%d.%d.%d:%d ...\r\n",
         ServerIP[0], ServerIP[1], ServerIP[2], ServerIP[3], SERVER_PORT);

  while (trials--)
  {
    if (WIFI_OpenClientConnection(TcpSocket, WIFI_TCP_PROTOCOL, "TCP_CLIENT", ServerIP, SERVER_PORT, 0) == WIFI_STATUS_OK)
    {
      printf("[OK] Connexion TCP etablie avec succes !\r\n");
      return true;
    }
    HAL_Delay(500);
  }

  printf("[ERREUR] Impossible de se connecter au serveur TCP.\r\n");
  return false;
}

static void Send_Telemetry(float temperature, float humidity, uint32_t adc_val,
                           uint8_t alert_door, uint8_t alert_temp)
{
  char tx[TX_BUFFER_SIZE];
  char temp_str[FLOAT_STR_LEN];
  char hum_str[FLOAT_STR_LEN];
  uint16_t sent_len = 0;

  Format_Float(temp_str, sizeof(temp_str), temperature);
  Format_Float(hum_str, sizeof(hum_str), humidity);

  snprintf(tx, sizeof(tx),
           "{\"temp\": %s, \"hum\": %s, \"adc\": %lu, \"alert_door\": %d, \"alert_temp\": %d}\n",
           temp_str, hum_str, (unsigned long)adc_val, alert_door, alert_temp);

  if (WIFI_SendData(TcpSocket, (uint8_t *)tx, (uint16_t)strlen(tx), &sent_len, WIFI_WRITE_TIMEOUT) == WIFI_STATUS_OK)
  {
    printf(" -> [TCP] Message envoye avec succes (%d octets)\r\n", sent_len);
  }
  else
  {
    printf(" -> [TCP ERREUR] Echec de l'envoi. Tentative de reconnexion au serveur...\r\n");
    WIFI_CloseClientConnection(TcpSocket);
    Connect_To_TCP_Server();
  }
}

/* ==========================================================================
 *  MESURES ET HISTORIQUE IA
 * ========================================================================== */

static uint32_t Read_ADC(uint32_t previous_value)
{
  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
  {
    return HAL_ADC_GetValue(&hadc1);
  }
  return previous_value;
}

static void History_Push(History_t *h, float temp, float hum)
{
  memmove(&h->temp[0], &h->temp[1], (FENETRE - 1U) * sizeof(float));
  memmove(&h->hum[0], &h->hum[1], (FENETRE - 1U) * sizeof(float));
  h->temp[FENETRE - 1U] = temp;
  h->hum[FENETRE - 1U] = hum;

  if (h->count < FENETRE)
  {
    h->count++;
  }
}

static Prediction_t AI_Predict(const History_t *h)
{
  Prediction_t result = {false, 0.0f, 0.0f};
  float delta_temp = 0.0f;
  float delta_hum = 0.0f;

  if (h->count < FENETRE)
  {
    return result; /* Pas assez de données pour prédire */
  }

  /* Appel du réseau de neurones */
  AI_prediction(h->temp, h->hum, &delta_temp, &delta_hum);

  result.valid = true;
  result.temp = h->temp[FENETRE - 1U] + delta_temp;
  result.hum = h->hum[FENETRE - 1U] + delta_hum;
  return result;
}

/* ==========================================================================
 *  GESTION CENTRALISÉE DES ALERTES
 * ========================================================================== */

static bool Above_Threshold(bool previous, float value, float threshold, float hysteresis)
{
  if (value > threshold)
  {
    return true;
  }
  if (value <= threshold - hysteresis)
  {
    return false;
  }
  return previous;
}

/**
 * @brief  Gère les états d'alertes avec temporisation porte et déclenche l'alarme matérielle.
 */
static void Manage_Alerts(AlertState_t *state, float current_temp, uint32_t adc_val,
                          const Prediction_t *pred, uint8_t *alert_door, uint8_t *alert_temp)
{
  /* 1. Évaluation porte avec temporisation (20 secondes au-dessus du seuil) */
  if ((float)adc_val > ADC_DOOR_THRESHOLD)
  {
    if (!state->door_timer_running)
    {
      state->door_timer_running = true;
      state->door_start_ms = HAL_GetTick();
    }
    else if ((HAL_GetTick() - state->door_start_ms) >= ADC_DOOR_DELAY_MS)
    {
      state->door_open = true;
    }
  }
  else if ((float)adc_val <= (ADC_DOOR_THRESHOLD - ADC_DOOR_HYSTERESIS))
  {
    state->door_timer_running = false;
    state->door_open = false;
  }

  /* 2. Évaluation des seuils de température (avec hystérésis) */
  state->temp_critical = Above_Threshold(state->temp_critical, current_temp,
                                         TEMP_THRESHOLD_C, TEMP_HYSTERESIS_C);

  state->temp_predicted = pred->valid &&
                          Above_Threshold(state->temp_predicted, pred->temp,
                                          TEMP_THRESHOLD_C, TEMP_HYSTERESIS_C);

  /* 3. Formatage des valeurs d'alerte */
  *alert_door = state->door_open ? 1U : 0U;

  if (state->temp_critical)
  {
    *alert_temp = 2U; /* Critique: Seuil actuel dépassé */
  }
  else if (state->temp_predicted)
  {
    *alert_temp = 1U; /* Préventif: Seuil prédit dépassé */
  }
  else
  {
    *alert_temp = 0U; /* Normal */
  }

  /* 4. Déclenchement matériel de l'alarme (Buzzer + LED) */
  if (*alert_temp > 0U)
  {
    HAL_GPIO_WritePin(ALARM_GPIO_PORT, ALARM_PINS, GPIO_PIN_SET);
  }
  else
  {
    HAL_GPIO_WritePin(ALARM_GPIO_PORT, ALARM_PINS, GPIO_PIN_RESET);
  }
}

/* ==========================================================================
 *  AFFICHAGE CONSOLE
 * ========================================================================== */

static void Format_Float(char *buf, size_t size, float value)
{
  const int centi = (int)(fabsf(value) * 100.0f + 0.5f);
  snprintf(buf, size, "%s%d.%02d",
           (value < 0.0f && centi != 0) ? "-" : "",
           centi / 100, centi % 100);
}

static void Log_Status(float temperature, float humidity, uint32_t adc_val,
                       uint8_t alert_door, uint8_t alert_temp,
                       const History_t *history, const Prediction_t *prediction)
{
  char temp_str[FLOAT_STR_LEN], hum_str[FLOAT_STR_LEN];
  Format_Float(temp_str, sizeof(temp_str), temperature);
  Format_Float(hum_str, sizeof(hum_str), humidity);

  printf("\r\n[Capteurs] Temp: %s C | Hum: %s %% | ADC: %lu | Porte: %d | Alerte Temp: %d\r\n",
         temp_str, hum_str, (unsigned long)adc_val, alert_door, alert_temp);

  if (prediction->valid)
  {
    Format_Float(temp_str, sizeof(temp_str), prediction->temp);
    Format_Float(hum_str, sizeof(hum_str), prediction->hum);
    printf("[IA] Prevision +%u min : Temp: %s C | Hum: %s %%\r\n",
           (unsigned int)AI_HORIZON_MIN, temp_str, hum_str);
  }
  else
  {
    printf("[IA] Historique en cours : %u/%u mesures\r\n",
           (unsigned int)history->count, (unsigned int)FENETRE);
  }
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
  /* USER CODE BEGIN 1 */
  float temperature = 0.0f;
  float humidity = 0.0f;
  uint32_t adc_val = 0;

  History_t history = {0};
  Prediction_t prediction = {0};
  AlertState_t alerts = {0};

  uint8_t alert_door = 0;
  uint8_t alert_temp = 0;

  uint32_t last_sample_ms;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();
  PeriphCommonClock_Config();

  MX_GPIO_Init();
  MX_DFSDM1_Init();
  MX_I2C2_Init();
  MX_QUADSPI_Init();
  MX_SPI3_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_ADC1_Init();

  /* USER CODE BEGIN 2 */
  printf("\r\n=======================================\r\n");
  printf("      Demarrage Systeme STM32 IoT      \r\n");
  printf("=======================================\r\n");

  BSP_TSENSOR_Init();
  BSP_HSENSOR_Init();
  printf("[OK] Capteurs initialises.\r\n");

  while (!Connect_To_WiFi())
  {
    printf("Nouvelle tentative Wi-Fi dans 3 secondes...\r\n");
    HAL_Delay(3000);
  }

  Connect_To_TCP_Server();

  last_sample_ms = HAL_GetTick() - SAMPLE_PERIOD_MS;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 1. LECTURE DES CAPTEURS */
    temperature = BSP_TSENSOR_ReadTemp();
    humidity = BSP_HSENSOR_ReadHumidity();
    adc_val = Read_ADC(adc_val);

    /* 2. HISTORIQUE ET PRÉDICTION IA */
    if ((HAL_GetTick() - last_sample_ms) >= SAMPLE_PERIOD_MS)
    {
      last_sample_ms = HAL_GetTick();
      History_Push(&history, temperature, humidity);
      prediction = AI_Predict(&history);
    }

    /* 3. ÉVALUATION ET EXÉCUTION DES ALERTES */
    Manage_Alerts(&alerts, temperature, adc_val, &prediction, &alert_door, &alert_temp);

    /* 4. AFFICHAGE CONSOLE */
    Log_Status(temperature, humidity, adc_val, alert_door, alert_temp, &history, &prediction);

    /* 5. ENVOI DES DONNÉES AU SERVEUR */
    Send_Telemetry(temperature, humidity, adc_val, alert_door, alert_temp);

    /* 6. PAUSE */
    HAL_Delay(LOOP_PERIOD_MS);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_RCCEx_EnableMSIPLLMode();
}

/**
 * @brief Peripherals Common Clock Configuration
 * @retval None
 */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_PLLSAI1;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLLSAI1;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_MSI;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 24;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_48M2CLK | RCC_PLLSAI1_ADC1CLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void)
{
  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_14;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief DFSDM1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_DFSDM1_Init(void)
{
  hdfsdm1_channel1.Instance = DFSDM1_Channel1;
  hdfsdm1_channel1.Init.OutputClock.Activation = ENABLE;
  hdfsdm1_channel1.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
  hdfsdm1_channel1.Init.OutputClock.Divider = 2;
  hdfsdm1_channel1.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
  hdfsdm1_channel1.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
  hdfsdm1_channel1.Init.Input.Pins = DFSDM_CHANNEL_FOLLOWING_CHANNEL_PINS;
  hdfsdm1_channel1.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
  hdfsdm1_channel1.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
  hdfsdm1_channel1.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
  hdfsdm1_channel1.Init.Awd.Oversampling = 1;
  hdfsdm1_channel1.Init.Offset = 0;
  hdfsdm1_channel1.Init.RightBitShift = 0x00;
  if (HAL_DFSDM_ChannelInit(&hdfsdm1_channel1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief I2C2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2C2_Init(void)
{
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x00000E14;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief QUADSPI Initialization Function
 * @param None
 * @retval None
 */
static void MX_QUADSPI_Init(void)
{
  hqspi.Instance = QUADSPI;
  hqspi.Init.ClockPrescaler = 2;
  hqspi.Init.FifoThreshold = 4;
  hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
  hqspi.Init.FlashSize = 23;
  hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_1_CYCLE;
  hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
  if (HAL_QSPI_Init(&hqspi) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief SPI3 Initialization Function
 * @param None
 * @retval None
 */
static void MX_SPI3_Init(void)
{
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 7;
  hspi3.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi3.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief USART1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief USART3 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief USB_OTG_FS Initialization Function
 * @param None
 * @retval None
 */
static void MX_USB_OTG_FS_PCD_Init(void)
{
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 6;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.battery_charging_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOE, M24SR64_Y_RF_DISABLE_Pin | M24SR64_Y_GPO_Pin | ISM43362_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, ARD_D10_Pin | SPBTLE_RF_RST_Pin | ARD_D9_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, ARD_D8_Pin | ISM43362_BOOT0_Pin | ISM43362_WAKEUP_Pin | LED2_Pin | SPSGRF_915_SDN_Pin | ARD_D5_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOD, USB_OTG_FS_PWR_EN_Pin | PMOD_RESET_Pin | STSAFE_A100_RESET_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SPBTLE_RF_SPI3_CSN_GPIO_Port, SPBTLE_RF_SPI3_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOC, VL53L0X_XSHUT_Pin | LED3_WIFI__LED4_BLE_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SPSGRF_915_SPI3_CSN_GPIO_Port, SPSGRF_915_SPI3_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ISM43362_SPI3_CSN_GPIO_Port, ISM43362_SPI3_CSN_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = M24SR64_Y_RF_DISABLE_Pin | M24SR64_Y_GPO_Pin | ISM43362_RST_Pin | ISM43362_SPI3_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = USB_OTG_FS_OVRCR_EXTI3_Pin | SPSGRF_915_GPIO3_EXTI5_Pin | SPBTLE_RF_IRQ_EXTI6_Pin | GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BUTTON_EXTI13_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BUTTON_EXTI13_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ARD_D1_Pin | ARD_D0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ARD_D10_Pin | SPBTLE_RF_RST_Pin | ARD_D9_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ARD_D4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(ARD_D4_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ARD_D13_Pin | ARD_D12_Pin | ARD_D11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ARD_D3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ARD_D3_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ARD_D8_Pin | ISM43362_BOOT0_Pin | ISM43362_WAKEUP_Pin | LED2_Pin | SPSGRF_915_SDN_Pin | ARD_D5_Pin | SPSGRF_915_SPI3_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LPS22HB_INT_DRDY_EXTI0_Pin | LSM6DSL_INT1_EXTI11_Pin | ARD_D2_Pin | HTS221_DRDY_EXTI15_Pin | PMOD_IRQ_EXTI12_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = USB_OTG_FS_PWR_EN_Pin | SPBTLE_RF_SPI3_CSN_Pin | PMOD_RESET_Pin | STSAFE_A100_RESET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = VL53L0X_XSHUT_Pin | LED3_WIFI__LED4_BLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = VL53L0X_GPIO1_EXTI7_Pin | LSM3MDL_DRDY_EXTI8_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PMOD_SPI2_SCK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(PMOD_SPI2_SCK_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PMOD_UART2_CTS_Pin | PMOD_UART2_RTS_Pin | PMOD_UART2_TX_Pin | PMOD_UART2_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = ARD_D15_Pin | ARD_D14_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);
}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  switch (GPIO_Pin)
  {
  case (GPIO_PIN_1):
  {
    SPI_WIFI_ISR();
    break;
  }
  default:
  {
    break;
  }
  }
}

int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}
/* USER CODE END 4 */

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
