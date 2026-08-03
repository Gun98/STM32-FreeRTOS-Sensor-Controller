/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app.h"
#include "app_types.h"
#include "hcsr04.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "uart_protocol.h"


#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  RESET_CAUSE_UNKNOWN = 0,
  RESET_CAUSE_POWER,
  RESET_CAUSE_EXTERNAL_PIN,
  RESET_CAUSE_SOFTWARE,
  RESET_CAUSE_IWDG,
  RESET_CAUSE_WWDG,
  RESET_CAUSE_LOW_POWER
} ResetCause_t;

typedef struct
{
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t xpsr;

  uint32_t exc_return;

  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t dfsr;
  uint32_t afsr;
  uint32_t bfar;
  uint32_t mmfar;
  uint32_t shcsr;
} HardFaultInfo_t;

typedef struct
{
  uint8_t valid;

  UartProtocolPacket_t request;

  uint16_t response_length;

  uint8_t response_frame[
      UART_PROTOCOL_MAX_FRAME_SIZE];

} UartProtocolTransactionCache_t;

typedef struct
{
  uint8_t rtos_objects_ok;

  uint8_t oled_ok;
  uint8_t eeprom_ok;
  uint8_t rtc_ok;

  /*
   * sensor_ready는 고장 판정이 아니다.
   * 현재 유효한 Snapshot이 준비됐는지만 나타낸다.
   */
  uint8_t sensor_ready;

} SystemSelfTest_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define SELF_TEST_OLED_ADDRESS      0x3CU
#define SELF_TEST_EEPROM_ADDRESS    0x57U
#define SELF_TEST_RTC_ADDRESS       0x68U

#define SELF_TEST_I2C_TRIALS        2U
#define SELF_TEST_I2C_TIMEOUT_MS    20U

#define EVENT_TIMER_TICK    (1UL << 0)
#define EVENT_BUTTON        (1UL << 1)
#define EVENT_SENSOR_VALID   (1UL << 2)

#define CONTROL_LED_TOGGLE    1U
#define CONTROL_LED_ON        2U
#define CONTROL_LED_OFF       3U

#define HEALTH_APP       (1UL << 0)
#define HEALTH_SENSOR    (1UL << 1)
#define HEALTH_MONITOR   (1UL << 2)

#define HEALTH_ALL \
    (HEALTH_APP | HEALTH_SENSOR | HEALTH_MONITOR)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

IWDG_HandleTypeDef hiwdg;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_tx;

/* Definitions for appTask */
osThreadId_t appTaskHandle;
const osThreadAttr_t appTask_attributes = {
  .name = "appTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for heartbeatTask */
osThreadId_t heartbeatTaskHandle;
const osThreadAttr_t heartbeatTask_attributes = {
  .name = "heartbeatTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for consumerTask */
osThreadId_t consumerTaskHandle;
const osThreadAttr_t consumerTask_attributes = {
  .name = "consumerTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for eventTask */
osThreadId_t eventTaskHandle;
const osThreadAttr_t eventTask_attributes = {
  .name = "eventTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for monitorTask */
osThreadId_t monitorTaskHandle;
const osThreadAttr_t monitorTask_attributes = {
  .name = "monitorTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for commandTask */
osThreadId_t commandTaskHandle;
const osThreadAttr_t commandTask_attributes = {
  .name = "commandTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for watchdogTask */
osThreadId_t watchdogTaskHandle;
const osThreadAttr_t watchdogTask_attributes = {
  .name = "watchdogTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for uartTxTask */
osThreadId_t uartTxTaskHandle;
const osThreadAttr_t uartTxTask_attributes = {
  .name = "uartTxTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for counterQueue */
osMessageQueueId_t counterQueueHandle;
const osMessageQueueAttr_t counterQueue_attributes = {
  .name = "counterQueue"
};
/* Definitions for controlQueue */
osMessageQueueId_t controlQueueHandle;
const osMessageQueueAttr_t controlQueue_attributes = {
  .name = "controlQueue"
};
/* Definitions for uartTxQueue */
osMessageQueueId_t uartTxQueueHandle;
const osMessageQueueAttr_t uartTxQueue_attributes = {
  .name = "uartTxQueue"
};
/* Definitions for statusTimer */
osTimerId_t statusTimerHandle;
const osTimerAttr_t statusTimer_attributes = {
  .name = "statusTimer"
};
/* Definitions for systemEvent */
osEventFlagsId_t systemEventHandle;
const osEventFlagsAttr_t systemEvent_attributes = {
  .name = "systemEvent"
};
/* Definitions for healthEvent */
osEventFlagsId_t healthEventHandle;
const osEventFlagsAttr_t healthEvent_attributes = {
  .name = "healthEvent"
};
/* USER CODE BEGIN PV */
static uint8_t uart_rx_byte = 0U;
static volatile uint32_t uart_rx_drop_count = 0U;

static ResetCause_t reset_cause =
    RESET_CAUSE_UNKNOWN;

volatile HardFaultInfo_t hardfault_info = {0};

static uint32_t reset_csr_raw = 0U;

static StreamBufferHandle_t
    uartRxStreamBufferHandle = NULL;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM2_Init(void);
static void MX_IWDG_Init(void);
void StartAppTask(void *argument);
void StartHeartbeatTask(void *argument);
void StartConsumerTask(void *argument);
void StartEventTask(void *argument);
void StartMonitorTask(void *argument);
void StartCommandTask(void *argument);
void StartWatchdogTask(void *argument);
void StartUartTxTask(void *argument);
void StatusTimerCallback(void *argument);

/* USER CODE BEGIN PFP */
static uint8_t RtosObjects_AreReady(void);

static ResetCause_t ResetCause_DetectFromRaw(
    uint32_t reset_flags)
{
  ResetCause_t cause =
      RESET_CAUSE_UNKNOWN;

  /*
   * 여러 Reset Flag가 동시에 Set될 수 있으므로
   * 구체적인 원인을 우선 판별한다.
   */
  if ((reset_flags & RCC_CSR_IWDGRSTF) != 0U)
  {
    cause = RESET_CAUSE_IWDG;
  }
  else if ((reset_flags & RCC_CSR_WWDGRSTF) != 0U)
  {
    cause = RESET_CAUSE_WWDG;
  }
  else if ((reset_flags & RCC_CSR_SFTRSTF) != 0U)
  {
    cause = RESET_CAUSE_SOFTWARE;
  }
  else if (((reset_flags & RCC_CSR_PORRSTF) != 0U) ||
           ((reset_flags & RCC_CSR_BORRSTF) != 0U))
  {
    cause = RESET_CAUSE_POWER;
  }
  else if ((reset_flags & RCC_CSR_PINRSTF) != 0U)
  {
    cause = RESET_CAUSE_EXTERNAL_PIN;
  }
  else if ((reset_flags & RCC_CSR_LPWRRSTF) != 0U)
  {
    cause = RESET_CAUSE_LOW_POWER;
  }
  else
  {
    cause = RESET_CAUSE_UNKNOWN;
  }

  return cause;
}
static const char *ResetCause_ToString(
    ResetCause_t cause);

static void MemoryReport_Print(void);

void vApplicationStackOverflowHook(
    TaskHandle_t xTask,
    char *pcTaskName);

void vApplicationMallocFailedHook(void);

static void UartProtocol_OnPacket(
    const UartProtocolPacket_t *packet);


static void UartProtocol_PrintFrameHex(
    const uint8_t *frame,
    uint16_t frame_length);

static void UartProtocol_SendAck(
    const UartProtocolPacket_t *request);

static void UartProtocol_SendError(
    const UartProtocolPacket_t *request,
    uint8_t error_code);

static UartProtocolTransactionCache_t
    transaction_cache =
{
  0
};


static uint32_t valid_packet_count = 0U;

static uint32_t duplicate_request_count = 0U;

static uint32_t tx_queue_failure_count = 0U;


static uint8_t
UartProtocol_IsDuplicateRequest(
    const UartProtocolPacket_t *packet);

static void
UartProtocol_StoreTransaction(
    const UartProtocolPacket_t *request,
    const uint8_t *response_frame,
    uint16_t response_length);

static HAL_StatusTypeDef
UartProtocol_ResendCachedResponse(void);

static HAL_StatusTypeDef
UartProtocol_SendResponseAndCache(
    const UartProtocolPacket_t *request,
    const UartProtocolPacket_t *response);

static HAL_StatusTypeDef
UartProtocol_QueueFrame(
    const uint8_t *frame,
    uint16_t frame_length);

static uint8_t SystemSelfTest_I2cReady(
    uint8_t address_7bit);

static void SystemSelfTest_Run(
    SystemSelfTest_t *result);

static void SystemSelfTest_Print(
    const SystemSelfTest_t *result);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
	reset_csr_raw = RCC->CSR;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  reset_cause =
      ResetCause_DetectFromRaw(reset_csr_raw);

  __HAL_RCC_CLEAR_RESET_FLAGS();

  #if defined(DEBUG)
    __HAL_DBGMCU_FREEZE_IWDG();
  #endif

  /*
   * Raw 값을 저장한 뒤 다음 Reset 판정을 위해
   * 하드웨어 Reset Flag를 Clear한다.
   */
  __HAL_RCC_CLEAR_RESET_FLAGS();
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_TIM4_Init();
  MX_TIM2_Init();
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */
  static const AppHardware_t hardware =
  {
      .i2c = &hi2c1,
      .uart = &huart2,

      .hcsr04_timer = &htim3,
      .hcsr04_channel = TIM_CHANNEL_1,
      .hcsr04_trig_port = GPIOB,
      .hcsr04_trig_pin = GPIO_PIN_5,

      .buzzer_timer = &htim4,
      .buzzer_channel = TIM_CHANNEL_1,

      .servo_timer = &htim2,
      .servo_channel = TIM_CHANNEL_3
  };

  App_Init(&hardware);

//  App_Init(&app_hardware);
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* Create the timer(s) */
  /* creation of statusTimer */
  statusTimerHandle = osTimerNew(StatusTimerCallback, osTimerPeriodic, NULL, &statusTimer_attributes);

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of counterQueue */
  counterQueueHandle = osMessageQueueNew (4, sizeof(SensorMessage_t), &counterQueue_attributes);

  /* creation of controlQueue */
  controlQueueHandle = osMessageQueueNew (8, sizeof(uint8_t), &controlQueue_attributes);

  /* creation of uartTxQueue */
  uartTxQueueHandle = osMessageQueueNew (4, sizeof(UartTxMessage_t), &uartTxQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /*
   * UART RX Byte Stream
   *
   * 전체 저장 공간: 64Byte
   * Trigger Level: 1Byte
   */
  uartRxStreamBufferHandle =
      xStreamBufferCreate(
          64U,
          1U);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of appTask */
  appTaskHandle = osThreadNew(StartAppTask, NULL, &appTask_attributes);

  /* creation of heartbeatTask */
  heartbeatTaskHandle = osThreadNew(StartHeartbeatTask, NULL, &heartbeatTask_attributes);

  /* creation of consumerTask */
  consumerTaskHandle = osThreadNew(StartConsumerTask, NULL, &consumerTask_attributes);

  /* creation of eventTask */
  eventTaskHandle = osThreadNew(StartEventTask, NULL, &eventTask_attributes);

  /* creation of monitorTask */
  monitorTaskHandle = osThreadNew(StartMonitorTask, NULL, &monitorTask_attributes);

  /* creation of commandTask */
  commandTaskHandle = osThreadNew(StartCommandTask, NULL, &commandTask_attributes);

  /* creation of watchdogTask */
  watchdogTaskHandle = osThreadNew(StartWatchdogTask, NULL, &watchdogTask_attributes);

  /* creation of uartTxTask */
  uartTxTaskHandle = osThreadNew(StartUartTxTask, NULL, &uartTxTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* Create the event(s) */
  /* creation of systemEvent */
  systemEventHandle = osEventFlagsNew(&systemEvent_attributes);

  /* creation of healthEvent */
  healthEventHandle = osEventFlagsNew(&healthEvent_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
  if (RtosObjects_AreReady() == 0U)
  {
    Error_Handler();
  }
  App_SetUartTxQueue(
      uartTxQueueHandle);
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
  hiwdg.Init.Reload = 3999;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 83;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 19999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 1500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 83;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_IC_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim3, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 83;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 499;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 250;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : USER_BUTTON_Pin */
  GPIO_InitStruct.Pin = USER_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_BUTTON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */


void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	 static uint32_t last_button_tick = 0U;

	  if (GPIO_Pin == USER_BUTTON_Pin)
	  {
	    uint32_t now = HAL_GetTick();

	    if ((now - last_button_tick) >= 50U)
	    {
	      uint8_t control_command =
	          CONTROL_LED_TOGGLE;

	      last_button_tick = now;

	      /*
	       * ISR에서는 기다릴 수 없으므로
	       * Queue Timeout은 반드시 0U로 사용한다.
	       */
	      if (controlQueueHandle != NULL)
	      {
	        (void)osMessageQueuePut(
	            controlQueueHandle,
	            &control_command,
	            0U,
	            0U);
	      }
	    }
	  }
}

void HAL_UART_RxCpltCallback(
    UART_HandleTypeDef *huart)
{
  BaseType_t higher_priority_task_woken =
      pdFALSE;

  size_t sent_length = 0U;

  if ((huart != NULL) &&
      (huart->Instance == USART2))
  {
    /*
     * UART ISR에서 받은 1Byte를
     * Stream Buffer에 저장한다.
     */
    if (uartRxStreamBufferHandle != NULL)
    {
      sent_length =
          xStreamBufferSendFromISR(
              uartRxStreamBufferHandle,
              &uart_rx_byte,
              1U,
              &higher_priority_task_woken);

      /*
       * Stream Buffer가 가득 차서
       * 1Byte를 넣지 못한 경우 Drop Count 증가.
       */
      if (sent_length != 1U)
      {
        uart_rx_drop_count++;
      }
    }
    else
    {
      uart_rx_drop_count++;
    }

    /*
     * 다음 1Byte UART 수신을 즉시 재등록한다.
     *
     * Task 전환 요청 전에 재등록해서
     * UART 수신 공백을 최대한 줄인다.
     */
    (void)HAL_UART_Receive_IT(
        &huart2,
        &uart_rx_byte,
        1U);

    /*
     * Stream Buffer를 기다리던 commandTask가
     * 깨어났고 우선순위가 더 높다면
     * ISR 종료 직후 실행한다.
     */
    portYIELD_FROM_ISR(
        higher_priority_task_woken);
  }
}

void HAL_UART_TxCpltCallback(
    UART_HandleTypeDef *huart)
{
  BaseType_t higher_priority_task_woken =
      pdFALSE;

  if ((huart != NULL) &&
      (huart->Instance == USART2))
  {
    if (uartTxTaskHandle != NULL)
    {
      /*
       * DMA 전송 완료를 uartTxTask에 직접 알린다.
       * 별도의 Binary Semaphore 객체가 필요하지 않다.
       */
      vTaskNotifyGiveFromISR(
          (TaskHandle_t)uartTxTaskHandle,
          &higher_priority_task_woken);

      /*
       * 깨어난 uartTxTask의 우선순위가 더 높다면
       * ISR 종료 직후 해당 Task로 전환한다.
       */
      portYIELD_FROM_ISR(
          higher_priority_task_woken);
    }
  }
}

void vApplicationStackOverflowHook(
    TaskHandle_t xTask,
    char *pcTaskName)
{
  /*
   * Debugger에서 xTask와 pcTaskName을 확인한다.
   * 메모리가 손상됐을 수 있으므로 UART는 사용하지 않는다.
   */
  (void)xTask;
  (void)pcTaskName;

  __disable_irq();

  /*
   * IWDG Refresh가 중단되어 자동 Reset된다.
   */
  for (;;)
  {
  }
}

void vApplicationMallocFailedHook(void)
{
  /*
   * FreeRTOS Heap 부족으로 동적 할당이 실패한 상태다.
   * Queue나 UART를 추가로 사용하지 않는다.
   */
  __disable_irq();

  /*
   * IWDG가 시스템을 Reset하도록 기다린다.
   */
  for (;;)
  {
  }
}

static void MemoryReport_Print(void)
{
  char buffer[160];

  size_t free_heap;
  size_t minimum_free_heap;

  UBaseType_t app_stack;
  UBaseType_t heartbeat_stack;
  UBaseType_t consumer_stack;
  UBaseType_t event_stack;
  UBaseType_t monitor_stack;
  UBaseType_t command_stack;
  UBaseType_t watchdog_stack;
  UBaseType_t uart_tx_stack;

  /*
   * 현재 남아 있는 FreeRTOS Heap과
   * 부팅 이후 최소 잔여 Heap을 읽는다.
   */
  free_heap =
      xPortGetFreeHeapSize();

  minimum_free_heap =
      xPortGetMinimumEverFreeHeapSize();

  /*
   * uxTaskGetStackHighWaterMark()의 반환값은
   * 해당 Task Stack의 최소 잔여 Word 수다.
   */
  app_stack =
      uxTaskGetStackHighWaterMark(
          (TaskHandle_t)appTaskHandle);

  heartbeat_stack =
      uxTaskGetStackHighWaterMark(
          (TaskHandle_t)heartbeatTaskHandle);

  consumer_stack =
      uxTaskGetStackHighWaterMark(
          (TaskHandle_t)consumerTaskHandle);

  event_stack =
      uxTaskGetStackHighWaterMark(
          (TaskHandle_t)eventTaskHandle);

  monitor_stack =
      uxTaskGetStackHighWaterMark(
          (TaskHandle_t)monitorTaskHandle);

  command_stack =
      uxTaskGetStackHighWaterMark(
          (TaskHandle_t)commandTaskHandle);

  watchdog_stack =
      uxTaskGetStackHighWaterMark(
          (TaskHandle_t)watchdogTaskHandle);

  uart_tx_stack =
      uxTaskGetStackHighWaterMark(
          (TaskHandle_t)uartTxTaskHandle);

  /*
   * Queue 메시지 크기가 160Byte이므로
   * 여러 묶음으로 나눠 출력한다.
   */
  (void)snprintf(
      buffer,
      sizeof(buffer),
      "\r\n[MEM] FREE HEAP     : %lu bytes\r\n"
      "[MEM] MIN FREE HEAP : %lu bytes\r\n",
      (unsigned long)free_heap,
      (unsigned long)minimum_free_heap);

  (void)App_UartTransmit(buffer);

  (void)snprintf(
      buffer,
      sizeof(buffer),
      "[MEM] APP       : %lu W / %lu B\r\n"
      "[MEM] HEARTBEAT : %lu W / %lu B\r\n"
      "[MEM] CONSUMER  : %lu W / %lu B\r\n",
      (unsigned long)app_stack,
      (unsigned long)(app_stack * sizeof(StackType_t)),
      (unsigned long)heartbeat_stack,
      (unsigned long)(heartbeat_stack * sizeof(StackType_t)),
      (unsigned long)consumer_stack,
      (unsigned long)(consumer_stack * sizeof(StackType_t)));

  (void)App_UartTransmit(buffer);

  (void)snprintf(
      buffer,
      sizeof(buffer),
      "[MEM] EVENT     : %lu W / %lu B\r\n"
      "[MEM] MONITOR   : %lu W / %lu B\r\n"
      "[MEM] COMMAND   : %lu W / %lu B\r\n",
      (unsigned long)event_stack,
      (unsigned long)(event_stack * sizeof(StackType_t)),
      (unsigned long)monitor_stack,
      (unsigned long)(monitor_stack * sizeof(StackType_t)),
      (unsigned long)command_stack,
      (unsigned long)(command_stack * sizeof(StackType_t)));

  (void)App_UartTransmit(buffer);

  (void)snprintf(
      buffer,
      sizeof(buffer),
      "[MEM] WATCHDOG  : %lu W / %lu B\r\n"
      "[MEM] UART TX   : %lu W / %lu B\r\n",
      (unsigned long)watchdog_stack,
      (unsigned long)(watchdog_stack * sizeof(StackType_t)),
      (unsigned long)uart_tx_stack,
      (unsigned long)(uart_tx_stack * sizeof(StackType_t)));

  (void)App_UartTransmit(buffer);
}


void HardFault_C(
    uint32_t *fault_stack,
    uint32_t exc_return)
{
  /*
   * Cortex-M Exception Stack Frame
   *
   * [0] R0
   * [1] R1
   * [2] R2
   * [3] R3
   * [4] R12
   * [5] LR
   * [6] PC
   * [7] xPSR
   */
  hardfault_info.r0 =
      fault_stack[0];

  hardfault_info.r1 =
      fault_stack[1];

  hardfault_info.r2 =
      fault_stack[2];

  hardfault_info.r3 =
      fault_stack[3];

  hardfault_info.r12 =
      fault_stack[4];

  hardfault_info.lr =
      fault_stack[5];

  hardfault_info.pc =
      fault_stack[6];

  hardfault_info.xpsr =
      fault_stack[7];

  hardfault_info.exc_return =
      exc_return;

  /*
   * Cortex-M Fault 상태 Register 저장
   */
  hardfault_info.cfsr =
      SCB->CFSR;

  hardfault_info.hfsr =
      SCB->HFSR;

  hardfault_info.dfsr =
      SCB->DFSR;

  hardfault_info.afsr =
      SCB->AFSR;

  hardfault_info.bfar =
      SCB->BFAR;

  hardfault_info.mmfar =
      SCB->MMFAR;

  hardfault_info.shcsr =
      SCB->SHCSR;

  /*
   * RTOS와 주변장치가 더 이상 실행되지 않도록 한다.
   */
  __disable_irq();

  /*
   * Debugger가 실제로 연결된 경우 자동으로 정지한다.
   *
   * Debugger 없이 Run 중이면 BKPT를 실행하지 않고,
   * IWDG가 시스템을 Reset하도록 기다린다.
   */
  if ((CoreDebug->DHCSR &
       CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
  {
    __BKPT(0);
  }

  for (;;)
  {
    /*
     * Debugger가 없다면 IWDG Refresh가 중단되어
     * 약 4초 뒤 MCU가 Reset된다.
     */
  }
}


static HAL_StatusTypeDef
UartProtocol_QueueFrame(
    const uint8_t *frame,
    uint16_t frame_length)
{
  HAL_StatusTypeDef tx_status;

  if ((frame == NULL) ||
      (frame_length == 0U) ||
      (frame_length >
       UART_PROTOCOL_MAX_FRAME_SIZE))
  {
    return HAL_ERROR;
  }

  tx_status =
      App_UartTransmitBytes(
          frame,
          frame_length);

  /*
   * Binary 응답을 uartTxQueue에 넣지 못했을 때
   * 통신 진단 카운터를 증가시킨다.
   */
  if (tx_status != HAL_OK)
  {
    tx_queue_failure_count++;
  }

  return tx_status;
}

static uint8_t
UartProtocol_IsDuplicateRequest(
    const UartProtocolPacket_t *packet)
{
  if (packet == NULL)
  {
    return 0U;
  }

  if (transaction_cache.valid == 0U)
  {
    return 0U;
  }

  /*
   * Sequence만 비교하면 안 된다.
   *
   * Version, Type, Sequence, Length가 모두 같고
   * 실제 Payload 내용도 같아야 동일 요청이다.
   */
  if (transaction_cache.request.version !=
      packet->version)
  {
    return 0U;
  }

  if (transaction_cache.request.type !=
      packet->type)
  {
    return 0U;
  }

  if (transaction_cache.request.sequence !=
      packet->sequence)
  {
    return 0U;
  }

  if (transaction_cache.request.length !=
      packet->length)
  {
    return 0U;
  }

  if (packet->length >
      UART_PROTOCOL_MAX_PAYLOAD_SIZE)
  {
    return 0U;
  }

  if (packet->length > 0U)
  {
    if (memcmp(
            transaction_cache.request.payload,
            packet->payload,
            packet->length) != 0)
    {
      return 0U;
    }
  }

  return 1U;
}


static void
UartProtocol_StoreTransaction(
    const UartProtocolPacket_t *request,
    const uint8_t *response_frame,
    uint16_t response_length)
{
  if ((request == NULL) ||
      (response_frame == NULL))
  {
    return;
  }

  if ((response_length == 0U) ||
      (response_length >
       UART_PROTOCOL_MAX_FRAME_SIZE))
  {
    return;
  }

  /*
   * 복사 도중에는 Cache를 유효하지 않은 상태로 둔다.
   *
   * 현재는 commandTask 하나에서만 접근하지만,
   * 저장 순서를 명확하게 유지하기 위한 처리다.
   */
  transaction_cache.valid = 0U;

  /*
   * 구조체 대입은 Payload 배열까지 함께 복사한다.
   */
  transaction_cache.request =
      *request;

  memcpy(
      transaction_cache.response_frame,
      response_frame,
      response_length);

  transaction_cache.response_length =
      response_length;

  /*
   * 모든 데이터 복사가 끝난 뒤 Cache를 유효하게 만든다.
   */
  transaction_cache.valid = 1U;
}


static HAL_StatusTypeDef
UartProtocol_ResendCachedResponse(void)
{
  if (transaction_cache.valid == 0U)
  {
    return HAL_ERROR;
  }

  if ((transaction_cache.response_length == 0U) ||
      (transaction_cache.response_length >
       UART_PROTOCOL_MAX_FRAME_SIZE))
  {
    return HAL_ERROR;
  }

  return UartProtocol_QueueFrame(
      transaction_cache.response_frame,
      transaction_cache.response_length);
}


static HAL_StatusTypeDef
UartProtocol_SendResponseAndCache(
    const UartProtocolPacket_t *request,
    const UartProtocolPacket_t *response)
{
  uint8_t response_frame[
      UART_PROTOCOL_MAX_FRAME_SIZE] =
  {
    0
  };

  uint16_t response_length;

  if ((request == NULL) ||
      (response == NULL))
  {
    return HAL_ERROR;
  }

  response_length =
      UartProtocol_EncodePacket(
          response,
          response_frame,
          sizeof(response_frame));

  if (response_length == 0U)
  {
    return HAL_ERROR;
  }

  /*
   * 응답 Queue 전송이 일시적으로 실패하더라도,
   * 요청 자체는 이미 처리되었을 수 있다.
   *
   * 따라서 Encode가 성공하면 먼저 Cache에 저장한다.
   * Retry가 들어오면 명령을 다시 실행하지 않고
   * 저장된 응답을 다시 보낼 수 있다.
   */
  UartProtocol_StoreTransaction(
      request,
      response_frame,
      response_length);

  return UartProtocol_QueueFrame(
      response_frame,
      response_length);
}

static uint8_t SystemSelfTest_I2cReady(
    uint8_t address_7bit)
{
  HAL_StatusTypeDef status;

  /*
   * HAL_I2C_IsDeviceReady()에는
   * 7Bit 주소를 왼쪽으로 1Bit 이동해서 전달한다.
   *
   * 예:
   * OLED 0x3C → 0x78
   */
  status =
      HAL_I2C_IsDeviceReady(
          &hi2c1,
          (uint16_t)(
              (uint16_t)address_7bit << 1U),
          SELF_TEST_I2C_TRIALS,
          SELF_TEST_I2C_TIMEOUT_MS);

  return (status == HAL_OK) ? 1U : 0U;
}


static void SystemSelfTest_Run(
    SystemSelfTest_t *result)
{
  SensorMessage_t sensor_snapshot =
  {
    0
  };

  if (result == NULL)
  {
    return;
  }

  memset(
      result,
      0,
      sizeof(*result));

  /*
   * Queue, Task, Event Flag 등
   * 필수 RTOS 객체가 생성됐는지 확인한다.
   */
  result->rtos_objects_ok =
      RtosObjects_AreReady();

  /*
   * I2C 주소별 ACK 응답 확인.
   */
  result->oled_ok =
      SystemSelfTest_I2cReady(
          SELF_TEST_OLED_ADDRESS);

  result->eeprom_ok =
      SystemSelfTest_I2cReady(
          SELF_TEST_EEPROM_ADDRESS);

  result->rtc_ok =
      SystemSelfTest_I2cReady(
          SELF_TEST_RTC_ADDRESS);

  /*
   * 센서는 현재 유효한 Snapshot이 있는지만 확인한다.
   *
   * 부팅 직후 Invalid라고 해서
   * 센서 고장으로 확정하지 않는다.
   */
  if ((App_GetSensorSnapshot(
          &sensor_snapshot) != 0U) &&
      (sensor_snapshot.valid != 0U))
  {
    result->sensor_ready = 1U;
  }
  else
  {
    result->sensor_ready = 0U;
  }
}


static void SystemSelfTest_Print(
    const SystemSelfTest_t *result)
{
  char buffer[96];

  uint8_t degraded = 0U;

  if (result == NULL)
  {
    return;
  }

  (void)App_UartTransmit(
      "\r\n=== SYSTEM SELF TEST ===\r\n");

  (void)snprintf(
      buffer,
      sizeof(buffer),
      "[SELF TEST] RTOS OBJECTS : %s\r\n",
      (result->rtos_objects_ok != 0U)
          ? "PASS"
          : "FAIL");

  (void)App_UartTransmit(buffer);

  (void)snprintf(
      buffer,
      sizeof(buffer),
      "[SELF TEST] OLED 0x3C    : %s\r\n",
      (result->oled_ok != 0U)
          ? "PASS"
          : "FAIL");

  (void)App_UartTransmit(buffer);

  (void)snprintf(
      buffer,
      sizeof(buffer),
      "[SELF TEST] EEPROM 0x57  : %s\r\n",
      (result->eeprom_ok != 0U)
          ? "PASS"
          : "FAIL");

  (void)App_UartTransmit(buffer);

  (void)snprintf(
      buffer,
      sizeof(buffer),
      "[SELF TEST] RTC 0x68     : %s\r\n",
      (result->rtc_ok != 0U)
          ? "PASS"
          : "FAIL");

  (void)App_UartTransmit(buffer);

  /*
   * 센서가 아직 준비되지 않은 것은
   * 즉시 하드웨어 FAIL로 판정하지 않는다.
   */
  (void)snprintf(
      buffer,
      sizeof(buffer),
      "[SELF TEST] SENSOR       : %s\r\n",
      (result->sensor_ready != 0U)
          ? "PASS"
          : "WAIT");

  (void)App_UartTransmit(buffer);

  /*
   * RTOS 객체 실패는 치명적 오류다.
   */
  if (result->rtos_objects_ok == 0U)
  {
    (void)App_UartTransmit(
        "[SELF TEST] RESULT       : FAIL\r\n");

    return;
  }

  /*
   * I2C 장치 일부가 응답하지 않아도
   * 가능한 기능은 계속 실행한다.
   */
  if ((result->oled_ok == 0U) ||
      (result->eeprom_ok == 0U) ||
      (result->rtc_ok == 0U))
  {
    degraded = 1U;
  }

  if (degraded != 0U)
  {
    (void)App_UartTransmit(
        "[SELF TEST] RESULT       : DEGRADED\r\n");
  }
  else
  {
    (void)App_UartTransmit(
        "[SELF TEST] RESULT       : PASS\r\n");
  }

  (void)App_UartTransmit(
      "========================\r\n");
}

static void UartProtocol_OnPacket(
    const UartProtocolPacket_t *packet)
{
  char log_buffer[128];

  HAL_StatusTypeDef binary_tx_status =
      HAL_ERROR;

  uint8_t control_value = 0U;

  UartProtocolPacket_t response_packet =
  {
    0
  };

  uint8_t response_frame[
      UART_PROTOCOL_MAX_FRAME_SIZE] =
  {
    0
  };

  uint16_t response_length = 0U;

  SensorMessage_t sensor_snapshot =
  {
    0
  };

  /*
   * 현재 Callback은 commandTask 문맥에서 호출된다.
   * ISR에서 실행되는 함수가 아니다.
   */
  if (packet == NULL)
  {
    return;
  }

  /*
   * 여기까지 전달된 Packet은 Parser에서
   * Version, Length, CRC 검사를 통과한 Packet이다.
   *
   * 중복 요청도 정상 CRC를 가진 Packet이므로
   * Valid Packet Count에는 포함한다.
   */
  valid_packet_count++;

  /*
   * 직전 요청과 Type, Sequence, Length, Payload가
   * 모두 같다면 Retry로 판단한다.
   *
   * switch문에 진입하지 않기 때문에 LED 제어와
   * Sensor Snapshot 같은 Application 동작은 재실행하지 않는다.
   */
  if (UartProtocol_IsDuplicateRequest(
          packet) != 0U)
  {
    duplicate_request_count++;

    binary_tx_status =
        UartProtocol_ResendCachedResponse();

    if (binary_tx_status == HAL_OK)
    {
      (void)snprintf(
          log_buffer,
          sizeof(log_buffer),
          "[PKT] DUPLICATE | "
          "TYPE 0x%02X | SEQ 0x%02X | "
          "REPLAY | VALID %lu | DUP %lu\r\n",
          (unsigned int)packet->type,
          (unsigned int)packet->sequence,
          (unsigned long)valid_packet_count,
          (unsigned long)duplicate_request_count);
    }
    else
    {
      (void)snprintf(
          log_buffer,
          sizeof(log_buffer),
          "[PKT] DUPLICATE | "
          "TYPE 0x%02X | SEQ 0x%02X | "
          "REPLAY FAILED | VALID %lu | DUP %lu\r\n",
          (unsigned int)packet->type,
          (unsigned int)packet->sequence,
          (unsigned long)valid_packet_count,
          (unsigned long)duplicate_request_count);
    }

    (void)App_UartTransmit(
        log_buffer);

    return;
  }

  switch (packet->type)
  {
    /*
     * PING 요청
     *
     * 정상 형식:
     * AA 55 01 01 SEQ 00 CRC_LOW CRC_HIGH
     */
    case UART_PACKET_TYPE_PING:

      if (packet->length != 0U)
      {
        UartProtocol_SendError(
            packet,
            UART_PROTOCOL_ERROR_INVALID_LENGTH);

        (void)snprintf(
            log_buffer,
            sizeof(log_buffer),
            "[PKT] PING ERROR | "
            "INVALID LENGTH %u\r\n",
            (unsigned int)packet->length);

        (void)App_UartTransmit(
            log_buffer);

        break;
      }

      (void)snprintf(
          log_buffer,
          sizeof(log_buffer),
          "[PKT] PING | SEQ 0x%02X | OK\r\n",
          (unsigned int)packet->sequence);

      (void)App_UartTransmit(
          log_buffer);

      response_packet.version =
          UART_PROTOCOL_VERSION;

      response_packet.type =
          UART_PACKET_TYPE_PONG;

      response_packet.sequence =
          packet->sequence;

      response_packet.length = 0U;

      response_length =
          UartProtocol_EncodePacket(
              &response_packet,
              response_frame,
              sizeof(response_frame));

      if (response_length > 0U)
      {
        /*
         * PING 처리는 이미 완료된 상태이므로
         * UART Queue 등록 전에 요청과 응답을 Cache한다.
         */
        UartProtocol_StoreTransaction(
            packet,
            response_frame,
            response_length);

        binary_tx_status =
            App_UartTransmitBytes(
                response_frame,
                response_length);

        if (binary_tx_status != HAL_OK)
        {
          (void)snprintf(
              log_buffer,
              sizeof(log_buffer),
              "[PKT] BINARY TX ERROR: %u\r\n",
              (unsigned int)binary_tx_status);

          (void)App_UartTransmit(
              log_buffer);
        }

        UartProtocol_PrintFrameHex(
            response_frame,
            response_length);
      }
      else
      {
        (void)App_UartTransmit(
            "[PKT] ENCODE ERROR\r\n");
      }

      break;


    /*
     * GET_STATUS 요청
     *
     * 정상 형식:
     * AA 55 01 02 SEQ 00 CRC_LOW CRC_HIGH
     */
    case UART_PACKET_TYPE_GET_STATUS:
    {
      uint16_t distance_tenth_cm;

      if (packet->length != 0U)
      {
        UartProtocol_SendError(
            packet,
            UART_PROTOCOL_ERROR_INVALID_LENGTH);

        (void)snprintf(
            log_buffer,
            sizeof(log_buffer),
            "[PKT] STATUS ERROR | "
            "INVALID LENGTH %u\r\n",
            (unsigned int)packet->length);

        (void)App_UartTransmit(
            log_buffer);

        break;
      }

      if (App_GetSensorSnapshot(
              &sensor_snapshot) == 0U)
      {
        UartProtocol_SendError(
            packet,
            UART_PROTOCOL_ERROR_SNAPSHOT_FAILED);

        (void)App_UartTransmit(
            "[PKT] STATUS ERROR | "
            "SNAPSHOT FAILED\r\n");

        break;
      }

      if (sensor_snapshot.distance_tenth_cm >
          UINT16_MAX)
      {
        distance_tenth_cm =
            UINT16_MAX;
      }
      else
      {
        distance_tenth_cm =
            (uint16_t)
                sensor_snapshot.distance_tenth_cm;
      }

      response_packet.version =
          UART_PROTOCOL_VERSION;

      response_packet.type =
          UART_PACKET_TYPE_STATUS;

      response_packet.sequence =
          packet->sequence;

      response_packet.length = 3U;

      response_packet.payload[0] =
          (uint8_t)(
              distance_tenth_cm &
              0x00FFU);

      response_packet.payload[1] =
          (uint8_t)(
              (distance_tenth_cm >> 8U) &
              0x00FFU);

      response_packet.payload[2] =
          sensor_snapshot.valid;

      if (UartProtocol_SendResponseAndCache(
              packet,
              &response_packet) != HAL_OK)
      {
        (void)App_UartTransmit(
            "[PKT] STATUS TX ERROR\r\n");

        break;
      }

      (void)snprintf(
          log_buffer,
          sizeof(log_buffer),
          "[PKT] STATUS | SEQ 0x%02X | "
          "DIST %lu.%lu cm | VALID %u\r\n",
          (unsigned int)packet->sequence,
          (unsigned long)(
              sensor_snapshot.distance_tenth_cm /
              10U),
          (unsigned long)(
              sensor_snapshot.distance_tenth_cm %
              10U),
          (unsigned int)sensor_snapshot.valid);

      (void)App_UartTransmit(
          log_buffer);

      break;
    }


    /*
     * LED_SET 요청
     *
     * 정상 형식:
     * AA 55 01 03 SEQ 01 VALUE CRC_LOW CRC_HIGH
     *
     * VALUE:
     * 0x00 = LED OFF
     * 0x01 = LED ON
     */
    case UART_PACKET_TYPE_LED_SET:

      if (packet->length != 1U)
      {
        UartProtocol_SendError(
            packet,
            UART_PROTOCOL_ERROR_INVALID_LENGTH);

        (void)snprintf(
            log_buffer,
            sizeof(log_buffer),
            "[PKT] LED_SET ERROR | "
            "INVALID LENGTH %u\r\n",
            (unsigned int)packet->length);

        (void)App_UartTransmit(
            log_buffer);

        break;
      }

      if (packet->payload[0] > 1U)
      {
        UartProtocol_SendError(
            packet,
            UART_PROTOCOL_ERROR_INVALID_PAYLOAD);

        (void)snprintf(
            log_buffer,
            sizeof(log_buffer),
            "[PKT] LED_SET ERROR | "
            "INVALID VALUE %u\r\n",
            (unsigned int)packet->payload[0]);

        (void)App_UartTransmit(
            log_buffer);

        break;
      }

      if (packet->payload[0] == 1U)
      {
        control_value =
            CONTROL_LED_ON;
      }
      else
      {
        control_value =
            CONTROL_LED_OFF;
      }

      /*
       * GPIO는 Packet Handler가 직접 제어하지 않는다.
       *
       * controlQueue
       * → eventTask
       * → PA5 LED
       */
      if (osMessageQueuePut(
              controlQueueHandle,
              &control_value,
              0U,
              20U) != osOK)
      {
        UartProtocol_SendError(
            packet,
            UART_PROTOCOL_ERROR_CONTROL_QUEUE_FULL);

        (void)App_UartTransmit(
            "[PKT] LED_SET ERROR | "
            "CONTROL QUEUE FULL\r\n");

        break;
      }

      /*
       * Queue에 제어 요청이 등록된 이후 ACK를 생성한다.
       * 요청과 ACK가 Cache되므로 동일 Retry에서는
       * controlQueue에 명령을 다시 넣지 않는다.
       */
      UartProtocol_SendAck(
          packet);

      (void)snprintf(
          log_buffer,
          sizeof(log_buffer),
          "[PKT] LED_SET | SEQ 0x%02X | "
          "VALUE %u | ACCEPTED\r\n",
          (unsigned int)packet->sequence,
          (unsigned int)packet->payload[0]);

      (void)App_UartTransmit(
          log_buffer);

      break;


    /*
     * 등록되지 않은 Packet Type
     */
    default:

      UartProtocol_SendError(
          packet,
          UART_PROTOCOL_ERROR_UNKNOWN_TYPE);

      (void)snprintf(
          log_buffer,
          sizeof(log_buffer),
          "[PKT] UNKNOWN TYPE 0x%02X | "
          "SEQ 0x%02X\r\n",
          (unsigned int)packet->type,
          (unsigned int)packet->sequence);

      (void)App_UartTransmit(
          log_buffer);

      break;
  }
}

static void UartProtocol_PrintFrameHex(
    const uint8_t *frame,
    uint16_t frame_length)
{
  /*
   * 이 함수는 commandTask 문맥 한 곳에서만
   * 사용하므로 정적 Buffer를 사용한다.
   *
   * Task Stack 사용량 증가도 줄일 수 있다.
   */
  static char hex_text[160];

  size_t text_index = 0U;
  uint16_t frame_index;
  int written;

  if ((frame == NULL) ||
      (frame_length == 0U))
  {
    return;
  }

  written =
      snprintf(
          hex_text,
          sizeof(hex_text),
          "[PKT TX HEX] ");

  if (written < 0)
  {
    return;
  }

  text_index = (size_t)written;

  for (frame_index = 0U;
       frame_index < frame_length;
       frame_index++)
  {
    if (text_index >=
        sizeof(hex_text))
    {
      break;
    }

    written =
        snprintf(
            &hex_text[text_index],
            sizeof(hex_text) - text_index,
            "%02X ",
            (unsigned int)frame[frame_index]);

    if (written < 0)
    {
      return;
    }

    if ((size_t)written >=
        (sizeof(hex_text) - text_index))
    {
      text_index =
          sizeof(hex_text) - 1U;

      break;
    }

    text_index +=
        (size_t)written;
  }

  if (text_index <
      (sizeof(hex_text) - 2U))
  {
    hex_text[text_index++] = '\r';
    hex_text[text_index++] = '\n';
    hex_text[text_index] = '\0';
  }
  else
  {
    hex_text[
        sizeof(hex_text) - 1U] = '\0';
  }

  (void)App_UartTransmit(
      hex_text);
}


static void UartProtocol_SendAck(
    const UartProtocolPacket_t *request)
{
  UartProtocolPacket_t response_packet =
  {
    0
  };

  if (request == NULL)
  {
    return;
  }

  response_packet.version =
      UART_PROTOCOL_VERSION;

  response_packet.type =
      UART_PACKET_TYPE_ACK;

  response_packet.sequence =
      request->sequence;

  response_packet.length = 2U;

  response_packet.payload[0] =
      request->type;

  response_packet.payload[1] =
      UART_PROTOCOL_RESULT_OK;

  if (UartProtocol_SendResponseAndCache(
          request,
          &response_packet) != HAL_OK)
  {
    (void)App_UartTransmit(
        "[PKT] ACK TX ERROR\r\n");
  }
}

static void UartProtocol_SendError(
    const UartProtocolPacket_t *request,
    uint8_t error_code)
{
  UartProtocolPacket_t response_packet =
  {
    0
  };

  if (request == NULL)
  {
    return;
  }

  response_packet.version =
      UART_PROTOCOL_VERSION;

  response_packet.type =
      UART_PACKET_TYPE_ERROR;

  response_packet.sequence =
      request->sequence;

  response_packet.length = 2U;

  response_packet.payload[0] =
      request->type;

  response_packet.payload[1] =
      error_code;

  if (UartProtocol_SendResponseAndCache(
          request,
          &response_packet) != HAL_OK)
  {
    (void)App_UartTransmit(
        "[PKT] ERROR RESPONSE TX FAILED\r\n");
  }
}

static uint8_t RtosObjects_AreReady(void)
{
  if ((appTaskHandle == NULL) ||
      (heartbeatTaskHandle == NULL) ||
      (consumerTaskHandle == NULL) ||
      (eventTaskHandle == NULL) ||
      (monitorTaskHandle == NULL) ||
      (commandTaskHandle == NULL) ||
      (watchdogTaskHandle == NULL) ||
      (uartTxTaskHandle == NULL) ||
      (counterQueueHandle == NULL) ||
      (controlQueueHandle == NULL) ||
      (uartTxQueueHandle == NULL) ||
      (uartRxStreamBufferHandle == NULL) ||
      (statusTimerHandle == NULL) ||
      (systemEventHandle == NULL) ||
      (healthEventHandle == NULL))
  {
    return 0U;
  }

  return 1U;
}



static const char *ResetCause_ToString(
    ResetCause_t cause)
{
  switch (cause)
  {
    case RESET_CAUSE_POWER:
      return "POWER ON";

    case RESET_CAUSE_EXTERNAL_PIN:
      return "EXTERNAL PIN";

    case RESET_CAUSE_SOFTWARE:
      return "SOFTWARE";

    case RESET_CAUSE_IWDG:
      return "IWDG";

    case RESET_CAUSE_WWDG:
      return "WWDG";

    case RESET_CAUSE_LOW_POWER:
      return "LOW POWER";

    case RESET_CAUSE_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartAppTask */
/**
  * @brief  Function implementing the appTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartAppTask */
void StartAppTask(void *argument)
{
  /* USER CODE BEGIN 5 */
	/* USER CODE BEGIN StartAppTask */

	  SensorMessage_t sensor_message = {0};
	  osStatus_t queue_status;
	  uint32_t last_queue_tick =
	      osKernelGetTickCount();





	  last_queue_tick = osKernelGetTickCount();

	  for (;;)
	  {
	    App_Run();

	    if (healthEventHandle != NULL)
	    {
	      (void)osEventFlagsSet(
	          healthEventHandle,
	          HEALTH_APP);
	    }

	    uint32_t now = osKernelGetTickCount();

	    if ((now - last_queue_tick) >= 200U)
	    {
	      last_queue_tick = now;

	      if (App_GetSensorSnapshot(&sensor_message) != 0U)
	      {
	        queue_status = osMessageQueuePut(
	            counterQueueHandle,
	            &sensor_message,
	            0U,
	            0U);

	        if (queue_status != osOK)
	        {
	          /* Queue 전송 실패 */
	        }
	      }
	    }

	    osDelay(1U);
	  }

	  /* USER CODE END StartAppTask */
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartHeartbeatTask */
/**
* @brief Function implementing the heartbeatTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartHeartbeatTask */
void StartHeartbeatTask(void *argument)
{
  /* USER CODE BEGIN StartHeartbeatTask */
  /* Infinite loop */
//	const uint32_t tick_frequency = osKernelGetTickFreq();
//	const uint32_t period_ticks = tick_frequency / 2U;
//
//	  uint32_t next_wakeup = osKernelGetTickCount();

	  for (;;)
	  {

//		  HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
		  osDelay(500U);
//	    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
//
//	    next_wakeup += period_ticks;
//	    osDelayUntil(next_wakeup);
	  }

  /* USER CODE END StartHeartbeatTask */
}

/* USER CODE BEGIN Header_StartConsumerTask */
/**
* @brief Function implementing the consumerTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartConsumerTask */
void StartConsumerTask(void *argument)
{
  /* USER CODE BEGIN StartConsumerTask */

	  SensorMessage_t received_message = {0};

	  (void)argument;

	  for (;;)
	  {
	    /*
	     * appTask가 보낸 센서 메시지를 기다린다.
	     */
	    if (osMessageQueueGet(
	            counterQueueHandle,
	            &received_message,
	            NULL,
	            osWaitForever) == osOK)
	    {
	      /*
	       * 실제 센서 유효 상태를 systemEvent에 반영한다.
	       */
	      if (systemEventHandle != NULL)
	      {
	        if (received_message.valid != 0U)
	        {
	          (void)osEventFlagsSet(
	              systemEventHandle,
	              EVENT_SENSOR_VALID);
	        }
	        else
	        {
	          (void)osEventFlagsClear(
	              systemEventHandle,
	              EVENT_SENSOR_VALID);
	        }
	      }

	      /*
	       * consumerTask가 Queue 메시지를 정상 처리했음을 보고한다.
	       */
	      if (healthEventHandle != NULL)
	      {
	        (void)osEventFlagsSet(
	            healthEventHandle,
	            HEALTH_SENSOR);
	      }
	    }
	  }

  /* USER CODE END StartConsumerTask */
}

/* USER CODE BEGIN Header_StartEventTask */
/**
* @brief Function implementing the eventTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartEventTask */
void StartEventTask(void *argument)
{
  /* USER CODE BEGIN StartEventTask */

	  uint8_t control_command = 0U;

	  (void)argument;

	  for (;;)
	  {
	    /*
	     * 버튼 ISR 또는 commandTask가 제어 명령을 넣을 때까지
	     * eventTask는 Blocked 상태로 기다린다.
	     */
	    if (osMessageQueueGet(
	            controlQueueHandle,
	            &control_command,
	            NULL,
	            osWaitForever) == osOK)
	    {
	      switch (control_command)
	      {
	        case CONTROL_LED_TOGGLE:

	          HAL_GPIO_TogglePin(
	              GPIOA,
	              GPIO_PIN_5);

	          /*
	           * 실제 사용자 버튼 입력이 처리됐음을
	           * monitorTask에 전달한다.
	           */
	          if (systemEventHandle != NULL)
	          {
	            (void)osEventFlagsSet(
	                systemEventHandle,
	                EVENT_BUTTON);
	          }

	          (void)App_UartTransmit(
	              "[EVENT] USER BUTTON PRESSED\r\n");

	          break;

	        case CONTROL_LED_ON:

	          HAL_GPIO_WritePin(
	              GPIOA,
	              GPIO_PIN_5,
	              GPIO_PIN_SET);

	          (void)App_UartTransmit(
	              "OK: LED ON\r\n");

	          break;

	        case CONTROL_LED_OFF:

	          HAL_GPIO_WritePin(
	              GPIOA,
	              GPIO_PIN_5,
	              GPIO_PIN_RESET);

	          (void)App_UartTransmit(
	              "OK: LED OFF\r\n");

	          break;

	        default:

	          (void)App_UartTransmit(
	              "ERR: INVALID CONTROL COMMAND\r\n");

	          break;
	      }
	    }
	  }

  /* USER CODE END StartEventTask */
}

/* USER CODE BEGIN Header_StartMonitorTask */
/**
* @brief Function implementing the monitorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartMonitorTask */
void StartMonitorTask(void *argument)
{
  /* USER CODE BEGIN StartMonitorTask */

	  uint32_t flags;
	  uint32_t current_flags;
	  uint32_t one_second_ticks;

	  (void)argument;

	  one_second_ticks = osKernelGetTickFreq();

	  if (one_second_ticks == 0U)
	  {
	    one_second_ticks = 1000U;
	  }

	  /*
	   * Periodic Software Timer 시작
	   */
	  (void)osTimerStart(
	      statusTimerHandle,
	      one_second_ticks);

	  for (;;)
	  {
	    /*
	     * Timer 또는 Button 이벤트를 기다린다.
	     */
	    flags = osEventFlagsWait(
	        systemEventHandle,
	        EVENT_TIMER_TICK | EVENT_BUTTON,
	        osFlagsWaitAny,
	        osWaitForever);

	    if ((flags & osFlagsError) != 0U)
	    {
	      continue;
	    }

	    /*
	     * Software Timer 이벤트 처리
	     */
	    if ((flags & EVENT_TIMER_TICK) != 0U)
	    {
	      current_flags =
	          osEventFlagsGet(systemEventHandle);

	      if ((current_flags & osFlagsError) != 0U)
	      {
	        (void)App_UartTransmit(
	            "[MONITOR] EVENT FLAGS ERROR\r\n");
	      }
	      else if ((current_flags &
	                EVENT_SENSOR_VALID) != 0U)
	      {
	        (void)App_UartTransmit(
	            "[MONITOR] TIMER | SENSOR VALID\r\n");
	      }
	      else
	      {
	        (void)App_UartTransmit(
	            "[MONITOR] TIMER | SENSOR INVALID\r\n");
	      }

	      /*
	       * monitorTask가 주기 처리를 완료했음을 보고한다.
	       */
	      if (healthEventHandle != NULL)
	      {
	        (void)osEventFlagsSet(
	            healthEventHandle,
	            HEALTH_MONITOR);
	      }
	    }

	    /*
	     * 사용자 버튼 이벤트 처리
	     */
	    if ((flags & EVENT_BUTTON) != 0U)
	    {
	      (void)App_UartTransmit(
	          "[MONITOR] USER BUTTON EVENT\r\n");
	    }
	  }

  /* USER CODE END StartMonitorTask */
}

/* USER CODE BEGIN Header_StartCommandTask */
/**
* @brief Function implementing the commandTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCommandTask */
void StartCommandTask(void *argument)
{
  /* USER CODE BEGIN StartCommandTask */

  uint8_t received_byte = 0U;
  uint8_t rx_chunk[16] = {0};

  size_t received_length = 0U;
  size_t rx_index = 0U;

  uint8_t control_value = 0U;
  uint8_t discard_until_enter = 0U;
  uint8_t previous_was_cr = 0U;

  uint32_t command_index = 0U;

  char command_buffer[32] = {0};
  char response_buffer[128] = {0};

  SensorMessage_t sensor_snapshot = {0};

  SystemSelfTest_t self_test =
  {
    0
  };

  (void)argument;

  /*
   * Binary Packet Parser 초기화 및
   * 완성 Packet Callback 등록.
   */
  UartProtocol_Init(
      UartProtocol_OnPacket);

  /*
   * RTOS 객체와 appTask 초기화가 완료될 시간을 준다.
   */
  osDelay(100U);

  /*
   * USART2 RX 인터럽트 수신을 최초 한 번 시작한다.
   * 이후 재수신은 HAL_UART_RxCpltCallback()에서 수행한다.
   */
  if (HAL_UART_Receive_IT(
          &huart2,
          &uart_rx_byte,
          1U) != HAL_OK)
  {
    (void)App_UartTransmit(
        "[CMD] UART RX START ERROR\r\n");
  }

  /*
   * 부팅 원인과 명령어 준비 메시지를 출력한다.
   */
  (void)snprintf(
      response_buffer,
      sizeof(response_buffer),
      "\r\n[BOOT] RESET CAUSE: %s"
      " | RCC_CSR=0x%08lX\r\n"
      "[CMD] READY - TYPE HELP\r\n",
      ResetCause_ToString(reset_cause),
      (unsigned long)reset_csr_raw);

  (void)App_UartTransmit(response_buffer);

  osDelay(300U);

   SystemSelfTest_Run(
       &self_test);

   SystemSelfTest_Print(
       &self_test);

  for (;;)
  {
    /*
     * Stream Buffer에 데이터가 들어올 때까지 기다린다.
     * 한 번에 최대 16Byte까지 받을 수 있다.
     */
    received_length =
        xStreamBufferReceive(
            uartRxStreamBufferHandle,
            rx_chunk,
            sizeof(rx_chunk),
			pdMS_TO_TICKS(20U));

    if (received_length == 0U)
    {
    	(void)UartProtocol_CheckTimeout(
    	      HAL_GetTick());

      continue;
    }

    /*
     * 이번에 받은 모든 Byte를 순서대로 Parser에 전달한다.
     */
    for (rx_index = 0U;
         rx_index < received_length;
         rx_index++)
    {
      received_byte = rx_chunk[rx_index];

      if (UartProtocol_ProcessByte(
              received_byte,
              HAL_GetTick()) != 0U)

      {
        /*
         * Binary Packet이 시작되면
         * 조립 중이던 불완전한 Text 명령은 폐기한다.
         */
        command_index = 0U;
        command_buffer[0] = '\0';

        discard_until_enter = 0U;
        previous_was_cr = 0U;

        continue;
      }

      /*
       * Enter 처리
       *
       * PuTTY 설정에 따라 Enter 입력은
       * '\r', '\n', "\r\n" 중 하나로 들어올 수 있다.
       */
      if ((received_byte == '\r') ||
          (received_byte == '\n'))
      {
        /*
         * CRLF에서 CR을 이미 처리했다면
         * 뒤이어 들어온 LF는 무시한다.
         */
        if ((received_byte == '\n') &&
            (previous_was_cr != 0U))
        {
          previous_was_cr = 0U;
          continue;
        }

        if (received_byte == '\r')
        {
          previous_was_cr = 1U;
        }
        else
        {
          previous_was_cr = 0U;
        }

        /*
         * 너무 긴 명령을 폐기 중이었다면
         * Enter에서 정상 수신 상태로 복귀한다.
         */
        if (discard_until_enter != 0U)
        {
          discard_until_enter = 0U;
          command_index = 0U;
          command_buffer[0] = '\0';

          (void)App_UartTransmit(
              "\r\n[CMD] ERROR: COMMAND TOO LONG\r\n");

          continue;
        }

        /*
         * 빈 Enter는 처리하지 않는다.
         */
        if (command_index == 0U)
        {
          continue;
        }

        /*
         * 명령 문자열 종료.
         */
        command_buffer[command_index] = '\0';

        /*
         * 완성된 명령어를 한 번만 Echo한다.
         */
        (void)snprintf(
            response_buffer,
            sizeof(response_buffer),
            "\r\n[CMD] %s\r\n",
            command_buffer);

        (void)App_UartTransmit(response_buffer);

        /*
         * HELP 명령
         */
        if (strcmp(command_buffer, "HELP") == 0)
        {
          (void)App_UartTransmit(
              "COMMANDS:\r\n"
              "  HELP\r\n"
              "  STATUS\r\n"
              "  MEM\r\n"
              "  LED ON\r\n"
              "  LED OFF\r\n"
        	  "  PKT TEST\r\n"
        	  "  PKT CRC TEST\r\n"
        	  "  PKT CRC BAD\r\n"
        	  "  PKT TIMEOUT TEST\r\n"
        	  "  PKT STAT\r\n"
        	  "  SELF TEST\r\n"
          #if defined(DEBUG)
        	  "  FAULT WATCHDOG\r\n"
              "  FAULT STACK\r\n"
              "  FAULT MALLOC\r\n"
              "  FAULT HARD\r\n"
          #endif
          );
        }

        /*
         * STATUS 명령
         */
        else if (strcmp(command_buffer, "STATUS") == 0)
        {
          if (App_GetSensorSnapshot(
                  &sensor_snapshot) != 0U)
          {
            (void)snprintf(
                response_buffer,
                sizeof(response_buffer),
                "STATUS | SEQ %lu | TICK %lu | "
                "DIST %lu.%lu cm | SENSOR %s\r\n",
                (unsigned long)sensor_snapshot.sequence,
                (unsigned long)sensor_snapshot.tick,
                (unsigned long)
                    (sensor_snapshot.distance_tenth_cm / 10U),
                (unsigned long)
                    (sensor_snapshot.distance_tenth_cm % 10U),
                (sensor_snapshot.valid != 0U)
                    ? "VALID"
                    : "INVALID");

            (void)App_UartTransmit(response_buffer);
          }
          else
          {
            (void)App_UartTransmit(
                "STATUS ERROR: SNAPSHOT UNAVAILABLE\r\n");
          }
        }

        /*
         * MEM 명령
         */
        else if (strcmp(command_buffer, "MEM") == 0)
        {
          MemoryReport_Print();
        }
        else if (strcmp(
                     command_buffer,
                     "PKT TEST") == 0)
        {
          /*
           * PING Packet
           *
           * SOF1    = AA
           * SOF2    = 55
           * VERSION = 01
           * TYPE    = 01 (PING)
           * SEQ     = 2A
           * LENGTH  = 00
           */
        	static const uint8_t test_packet[] =
        	{
        	    0xAAU,  /* SOF1 */
        	    0x55U,  /* SOF2 */
        	    0x01U,  /* VERSION */
        	    0x01U,  /* PING */
        	    0x2AU,  /* SEQUENCE */
        	    0x00U,  /* LENGTH */

        	    /*
        	     * CRC-16/CCITT-FALSE
        	     * 대상: 01 01 2A 00
        	     * 결과: 0x2C69
        	     * 전송: 69 2C
        	     */
        	    0x69U,
        	    0x2CU
        	};

          size_t test_index;

          for (test_index = 0U;
               test_index < sizeof(test_packet);
               test_index++)
          {
        	  (void)UartProtocol_ProcessByte(
        		  test_packet[test_index],
        	      HAL_GetTick());
          }
        }

        else if (strcmp(
                     command_buffer,
                     "PKT CRC TEST") == 0)
        {
          static const uint8_t crc_test_data[] =
          {
            0x01U,  /* VERSION */
            0x01U,  /* PING TYPE */
            0x10U,  /* SEQUENCE */
            0x00U   /* LENGTH */
          };

          uint16_t calculated_crc;

          calculated_crc =
              UartProtocol_CalculateCrc(
                  crc_test_data,
                  (uint16_t)sizeof(crc_test_data));

          (void)snprintf(
              response_buffer,
              sizeof(response_buffer),
              "[CRC TEST] DATA 01 01 10 00 | "
              "CRC 0x%04X | TX %02X %02X | %s\r\n",
              (unsigned int)calculated_crc,
              (unsigned int)(
                  calculated_crc & 0x00FFU),
              (unsigned int)(
                  (calculated_crc >> 8U) & 0x00FFU),
              (calculated_crc == 0xC637U)
                  ? "PASS"
                  : "FAIL");

          (void)App_UartTransmit(
              response_buffer);
        }

        else if (strcmp(
                     command_buffer,
                     "SELF TEST") == 0)
        {
          SystemSelfTest_Run(
              &self_test);

          SystemSelfTest_Print(
              &self_test);
        }

        else if (strcmp(
                     command_buffer,
                     "PKT CRC BAD") == 0)
        {
          /*
           * 정상 PING Packet:
           *
           * AA 55 01 01 2A 00 69 2C
           *
           * 정상 CRC는 0x2C69이고,
           * 전송 순서는 69 2C이다.
           *
           * 아래에서는 마지막 CRC HIGH Byte를
           * 2C가 아니라 2D로 일부러 손상시킨다.
           */
          static const uint8_t bad_crc_packet[] =
          {
            0xAAU,
            0x55U,
            0x01U,
            0x01U,
            0x2AU,
            0x00U,
            0x69U,
            0x2DU
          };

          uint32_t error_count_before;
          uint32_t error_count_after;
          size_t test_index;

          error_count_before =
              UartProtocol_GetCrcErrorCount();

          for (test_index = 0U;
               test_index < sizeof(bad_crc_packet);
               test_index++)
          {
        	  (void)UartProtocol_ProcessByte(
        		  bad_crc_packet[test_index],
        	      HAL_GetTick());
          }

          error_count_after =
              UartProtocol_GetCrcErrorCount();

          (void)snprintf(
              response_buffer,
              sizeof(response_buffer),
              "[CRC BAD TEST] BEFORE %lu | "
              "AFTER %lu | %s\r\n",
              (unsigned long)error_count_before,
              (unsigned long)error_count_after,
              (error_count_after ==
               (error_count_before + 1U))
                  ? "PASS"
                  : "FAIL");

          (void)App_UartTransmit(
              response_buffer);
        }

        else if (strcmp(
                     command_buffer,
                     "PKT STAT") == 0)
        {
        	(void)snprintf(
        	      response_buffer,
        	      sizeof(response_buffer),
        	      "[PKT STAT] VALID %lu | "
        	      "DUPLICATE %lu\r\n",
        	      (unsigned long)valid_packet_count,
        	      (unsigned long)duplicate_request_count);

        	  (void)App_UartTransmit(
        	      response_buffer);

        	  (void)snprintf(
        	      response_buffer,
        	      sizeof(response_buffer),
        	      "[PKT STAT] CRC ERROR %lu | "
        	      "TIMEOUT %lu | RX DROP %lu | "
        	      "TX FAIL %lu\r\n",
        	      (unsigned long)
        	          UartProtocol_GetCrcErrorCount(),
        	      (unsigned long)
        	          UartProtocol_GetTimeoutErrorCount(),
        	      (unsigned long)uart_rx_drop_count,
        	      (unsigned long)tx_queue_failure_count);

        	  (void)App_UartTransmit(
        	      response_buffer);
        }

        else if (strcmp(
                     command_buffer,
                     "PKT TIMEOUT TEST") == 0)
        {
          /*
           * LED_SET Packet의 앞부분만 넣고 중간에서 끊는다.
           *
           * Parser 상태:
           * AA → WAIT_SOF2
           * 55 → READ_VERSION
           * 01 → READ_TYPE
           * 03 → READ_SEQUENCE
           */
          static const uint8_t partial_packet[] =
          {
            0xAAU,
            0x55U,
            0x01U,
            0x03U
          };

          uint32_t timeout_before;
          uint32_t timeout_after;
          uint8_t timeout_detected;
          size_t test_index;

          timeout_before =
              UartProtocol_GetTimeoutErrorCount();

          for (test_index = 0U;
               test_index < sizeof(partial_packet);
               test_index++)
          {
            (void)UartProtocol_ProcessByte(
                partial_packet[test_index],
                HAL_GetTick());
          }

          /*
           * RX Timeout 100ms보다 길게 기다린다.
           * 테스트 명령에서만 사용하는 의도적인 지연이다.
           */
          osDelay(150U);

          timeout_detected =
              UartProtocol_CheckTimeout(
                  HAL_GetTick());

          timeout_after =
              UartProtocol_GetTimeoutErrorCount();

          (void)snprintf(
              response_buffer,
              sizeof(response_buffer),
              "[TIMEOUT TEST] BEFORE %lu | "
              "AFTER %lu | DETECTED %u | "
              "ACTIVE %u | %s\r\n",
              (unsigned long)timeout_before,
              (unsigned long)timeout_after,
              (unsigned int)timeout_detected,
              (unsigned int)UartProtocol_IsActive(),
              ((timeout_after ==
                (timeout_before + 1U)) &&
               (timeout_detected == 1U) &&
               (UartProtocol_IsActive() == 0U))
                  ? "PASS"
                  : "FAIL");

          (void)App_UartTransmit(
              response_buffer);
        }

      #if defined(DEBUG)

        /*
         * Stack Overflow Hook 경로 시험
         */
        else if (strcmp(command_buffer, "FAULT STACK") == 0)
        {
          (void)App_UartTransmit(
              "[FAULT TEST] ENTER STACK OVERFLOW HOOK\r\n");

          osDelay(200U);

          vApplicationStackOverflowHook(
              (TaskHandle_t)commandTaskHandle,
              "FAULT TEST");
        }

        /*
         * Malloc Failed Hook 경로 시험
         */
        else if (strcmp(command_buffer, "FAULT MALLOC") == 0)
        {
          (void)App_UartTransmit(
              "[FAULT TEST] ENTER MALLOC FAILED HOOK\r\n");

          osDelay(200U);

          vApplicationMallocFailedHook();
        }

        /*
         * HardFault 경로 시험
         */
        else if (strcmp(command_buffer, "FAULT HARD") == 0)
        {
          (void)App_UartTransmit(
              "[FAULT TEST] TRIGGER HARDFAULT\r\n");

          /*
           * 안내 문장이 UART DMA로 전송될 시간을 준다.
           */
          osDelay(200U);

          __asm volatile ("udf #0");
        }

        else if (strcmp(
                     command_buffer,
                     "FAULT WATCHDOG") == 0)
        {
          (void)App_UartTransmit(
              "[FAULT TEST] APP TASK SUSPEND\r\n"
              "[FAULT TEST] WAIT FOR IWDG RESET\r\n");

          /*
           * 안내 문장이 UART DMA로 전송될 시간을 준다.
           */
          osDelay(200U);

          /*
           * HEALTH_APP을 보고하는 appTask를 강제로 정지한다.
           */
          vTaskSuspend(
              (TaskHandle_t)appTaskHandle);

          /*
           * 이미 설정돼 있을 수 있는 HEALTH_APP Bit도 지운다.
           * 이후 appTask가 정지돼 있으므로 다시 설정되지 않는다.
           */
          if (healthEventHandle != NULL)
          {
            (void)osEventFlagsClear(
                healthEventHandle,
                HEALTH_APP);
          }
        }

      #endif

        /*
         * LED ON 명령
         *
         * commandTask가 GPIO를 직접 제어하지 않고,
         * controlQueue를 통해 eventTask에 요청한다.
         */
        else if (strcmp(command_buffer, "LED ON") == 0)
        {
          control_value = CONTROL_LED_ON;

          if (osMessageQueuePut(
                  controlQueueHandle,
                  &control_value,
                  0U,
                  20U) != osOK)
          {
            (void)App_UartTransmit(
                "ERROR: CONTROL QUEUE FULL\r\n");
          }
        }

        /*
         * LED OFF 명령
         */
        else if (strcmp(command_buffer, "LED OFF") == 0)
        {
          control_value = CONTROL_LED_OFF;

          if (osMessageQueuePut(
                  controlQueueHandle,
                  &control_value,
                  0U,
                  20U) != osOK)
          {
            (void)App_UartTransmit(
                "ERROR: CONTROL QUEUE FULL\r\n");
          }
        }

        /*
         * 등록되지 않은 명령
         */
        else
        {
          (void)snprintf(
              response_buffer,
              sizeof(response_buffer),
              "UNKNOWN COMMAND: %s\r\n"
              "TYPE HELP\r\n",
              command_buffer);

          (void)App_UartTransmit(response_buffer);
        }

        /*
         * 다음 명령을 받을 수 있도록 초기화한다.
         */
        command_index = 0U;
        command_buffer[0] = '\0';

        /*
         * 현재 Enter 처리를 끝내고,
         * rx_chunk 안의 다음 Byte를 처리한다.
         */
        continue;
      }

      /*
       * 일반 문자가 들어오면 CRLF 상태를 해제한다.
       */
      previous_was_cr = 0U;

      /*
       * 긴 명령을 폐기 중이라면
       * Enter가 들어올 때까지 나머지 문자를 무시한다.
       */
      if (discard_until_enter != 0U)
      {
        continue;
      }

      /*
       * 마지막 '\0' 공간을 남기고 문자를 저장한다.
       */
      if (command_index <
          (sizeof(command_buffer) - 1U))
      {
        /*
         * 소문자를 대문자로 변환한다.
         *
         * help, Help, HELP를 모두 동일하게 처리한다.
         */
        if ((received_byte >= (uint8_t)'a') &&
            (received_byte <= (uint8_t)'z'))
        {
          received_byte =
              (uint8_t)(
                  received_byte -
                  ((uint8_t)'a' -
                   (uint8_t)'A'));
        }

        command_buffer[command_index] =
            (char)received_byte;

        command_index++;
      }
      else
      {
        /*
         * Buffer가 가득 차면 현재 명령을 폐기하고
         * 다음 Enter가 들어올 때까지 대기한다.
         */
        discard_until_enter = 1U;
        command_index = 0U;
        command_buffer[0] = '\0';
      }
    }
  }

  /* USER CODE END StartCommandTask */
}

/* USER CODE BEGIN Header_StartWatchdogTask */
/**
* @brief Function implementing the watchdogTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartWatchdogTask */
void StartWatchdogTask(void *argument)
{
  /* USER CODE BEGIN StartWatchdogTask */

	  uint32_t health_flags;
	  uint32_t health_timeout_ticks;
	  uint32_t idle_delay_ticks;

	  (void)argument;

	  /*
	   * 주요 Task의 Health 보고를 최대 약 2초간 기다린다.
	   */
	  health_timeout_ticks =
	      osKernelGetTickFreq() * 2U;

	  idle_delay_ticks =
	      osKernelGetTickFreq();

	  if (health_timeout_ticks == 0U)
	  {
	    health_timeout_ticks = 2000U;
	  }

	  if (idle_delay_ticks == 0U)
	  {
	    idle_delay_ticks = 1000U;
	  }

	  /*
	   * Scheduler 시작 직후 각 Task가 최초 Health를
	   * 보고할 수 있도록 한 번 Refresh해 초기 여유를 준다.
	   */
	  if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK)
	  {
	    Error_Handler();
	  }

	  for (;;)
	  {
	    health_flags = osEventFlagsWait(
	        healthEventHandle,
	        HEALTH_ALL,
	        osFlagsWaitAll,
	        health_timeout_ticks);

	    /*
	     * 세 Task가 모두 제한 시간 안에 보고했을 때만 Refresh.
	     * Wait 성공 시 Health Bit들은 기본적으로 자동 Clear된다.
	     */
	    if ((health_flags & osFlagsError) == 0U)
	    {
	      if (HAL_IWDG_Refresh(&hiwdg) != HAL_OK)
	      {
	        Error_Handler();
	      }
	    }
	    else
	    {
	      /*
	       * 하나 이상의 Task가 Health를 보고하지 못했다.
	       * 이제부터 Watchdog을 갱신하지 않고 Reset을 기다린다.
	       *
	       * 여기서 UART 로그를 출력하지 않는 이유:
	       * UART 또는 Mutex 고장이 원인일 수도 있기 때문이다.
	       */
	      for (;;)
	      {
	        osDelay(idle_delay_ticks);
	      }
	    }
	  }

  /* USER CODE END StartWatchdogTask */
}

/* USER CODE BEGIN Header_StartUartTxTask */
/**
* @brief Function implementing the uartTxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUartTxTask */
void StartUartTxTask(void *argument)
{
  /* USER CODE BEGIN StartUartTxTask */

  UartTxMessage_t tx_message = {0};

  (void)argument;

  for (;;)
  {
    /*
     * App_UartTransmit()이 uartTxQueue에 넣은
     * 메시지가 들어올 때까지 Block 상태로 기다린다.
     */
    if (osMessageQueueGet(
            uartTxQueueHandle,
            &tx_message,
            NULL,
            osWaitForever) != osOK)
    {
      continue;
    }

    /*
     * 송신 길이가 잘못된 메시지는 폐기한다.
     */
    if ((tx_message.length == 0U) ||
        (tx_message.length >= UART_TX_MESSAGE_SIZE))
    {
      continue;
    }

    /*
     * 이전 DMA 전송에서 남아 있을 수 있는
     * 오래된 Notification을 DMA 시작 전에 제거한다.
     */
    (void)ulTaskNotifyTake(
        pdTRUE,
        0U);

    /*
     * Queue에서 꺼낸 메시지를 UART DMA로 송신한다.
     *
     * tx_message는 uartTxTask의 지역변수지만,
     * DMA 완료까지 아래에서 Block 상태로 기다리므로
     * 송신 중에도 Buffer가 사라지지 않는다.
     */
    if (HAL_UART_Transmit_DMA(
            &huart2,
            (uint8_t *)tx_message.data,
            tx_message.length) != HAL_OK)
    {
      /*
       * DMA 시작 실패 시 UART 송신 상태를 복구한다.
       */
      (void)HAL_UART_AbortTransmit(
          &huart2);

      continue;
    }

    /*
     * HAL_UART_TxCpltCallback()이 보내는
     * Task Notification을 최대 1초 기다린다.
     */
    if (ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(1000U)) == 0U)
    {
      /*
       * 완료 Notification이 오지 않았다면
       * DMA 또는 UART 이상으로 판단한다.
       */
      (void)HAL_UART_AbortTransmit(
          &huart2);
    }
  }

  /* USER CODE END StartUartTxTask */
}

/* StatusTimerCallback function */
void StatusTimerCallback(void *argument)
{
  /* USER CODE BEGIN StatusTimerCallback */

	  (void)argument;

	  if (systemEventHandle != NULL)
	  {
	    (void)osEventFlagsSet(
	        systemEventHandle,
	        EVENT_TIMER_TICK);
	  }

  /* USER CODE END StatusTimerCallback */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM5 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM5)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
