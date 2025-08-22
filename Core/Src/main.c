/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct _can_message {
  FDCAN_RxHeaderTypeDef header;
  uint8_t data[8];
} can_message_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MESSAGE_QUEUE_SIZE 32
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

FDCAN_HandleTypeDef hfdcan1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
can_message_t message_queue[MESSAGE_QUEUE_SIZE];
uint32_t q_head = 0;
uint32_t q_tail = 0;
uint8_t q_full = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void send_test_can_message()
{
  FDCAN_TxHeaderTypeDef TxHeader;
  uint8_t TxData[8] = {0xDE, 0xAD, 0xBE, 0xEF};

  TxHeader.Identifier = 0x303;
  TxHeader.IdType = FDCAN_STANDARD_ID;
  TxHeader.TxFrameType = FDCAN_DATA_FRAME;
  TxHeader.DataLength = FDCAN_DLC_BYTES_4;
  TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
  TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
  TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  TxHeader.MessageMarker = 0;

  if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, TxData) != HAL_OK)
  {
    Error_Handler();
  }
}

void put_string(const char* message)
{ 
  HAL_UART_Transmit(&huart1, (const uint8_t *)message, strlen(message), 1000);
}

void put_hex(uint8_t data)
{
    uint8_t elem = data >> 4;
    int i;
    char message[3];
    message[0] = ' ';
    for (i = 0; i < 2; ++i) {
        elem = (elem < 10) ? elem + '0' : elem + 'a' - 10;
        message[i + 1] = elem;
        elem = data & 0x0f;
    }
    HAL_UART_Transmit(&huart1, (const uint8_t *)message, 3, 1000);
}

void process_command(char *command)
{
  if (strlen(command) == 0) {
    return;
  }
  if (strcmp(command, "tx") == 0) {
    send_test_can_message();
  } else {
    put_string(command);
    put_string(": Unknown command\r\n");
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

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FDCAN1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  char command[64];
  int index = 0;
  put_string("******************************\r\n");
  put_string("  CAN Bus Monitor\r\n");
  put_string("******************************\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (q_head != q_tail || q_full) {
      __disable_irq();
      FDCAN_RxHeaderTypeDef *rx_header = &message_queue[q_tail].header;
      uint8_t *rx_data = message_queue[q_tail].data;
      q_tail = q_tail + 1 % MESSAGE_QUEUE_SIZE;
      q_full = 0;
      __enable_irq();
      if (rx_header->IdType == FDCAN_STANDARD_ID) {
        put_string("std[");
        put_hex((rx_header->Identifier >> 8) & 0xff);
        put_hex(rx_header->Identifier & 0xff);
      } else {
        put_string("ext[");
        put_hex((rx_header->Identifier >> 24) & 0xff);
        put_hex((rx_header->Identifier >> 16) & 0xff);
        put_hex((rx_header->Identifier >> 8) & 0xff);
        put_hex(rx_header->Identifier & 0xff);
      }
      put_string(" ]:");
      for (uint32_t i = 0; i < rx_header->DataLength; ++i) {
        put_hex(rx_data[i]);
      }
      put_string("\r\n");
    }
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
      uint8_t data[1];
      if (HAL_UART_Receive(&huart1, data, sizeof(data), 1000) != HAL_OK) {
        Error_Handler();
      }
      HAL_UART_Transmit(&huart1, data, sizeof(data), 1000);
      if (data[0] == '\r') {
        data[0] = '\n';
        HAL_UART_Transmit(&huart1, data, sizeof(data), 1000);
        command[index] = '\0';
        index = 0;
        process_command(command);
      } else if (index < sizeof(command) - 1) {
        command[index++] = data[0];
      }
    }

    if (HAL_GPIO_ReadPin(USER_SW_GPIO_Port, USER_SW_Pin) == 0) {
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      send_test_can_message();
      HAL_Delay(100);
      while (HAL_GPIO_ReadPin(USER_SW_GPIO_Port, USER_SW_Pin) == 0) {}
    }
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

  __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_0);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */

  /* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = ENABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 1;
  hfdcan1.Init.NominalSyncJumpWidth = 2;
  hfdcan1.Init.NominalTimeSeg1 = 14;
  hfdcan1.Init.NominalTimeSeg2 = 9;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 2;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.StdFiltersNbr = 0;
  hfdcan1.Init.ExtFiltersNbr = 0;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */
  if (HAL_FDCAN_ConfigGlobalFilter(
        &hfdcan1, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
        FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK)
  {
    Error_Handler();
  }

  // Start FDCAN
  if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }

  // Activate reception notification
  HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
  /* USER CODE END FDCAN1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USER_SW_Pin */
  GPIO_InitStruct.Pin = USER_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(USER_SW_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
  if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0)
  {
    FDCAN_RxHeaderTypeDef dummy_header;
    uint8_t dummy_data[8];
    while (1)
    {
      FDCAN_RxHeaderTypeDef *rx_header;
      uint8_t *rx_data;
      if (q_full)
      {
        rx_header = &dummy_header;
        rx_data = dummy_data;
      }
      else
      {
        rx_header = &message_queue[q_head].header;
        rx_data = message_queue[q_head].data;
      }

      if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, rx_header, rx_data) != HAL_OK)
      {
        if (hfdcan->ErrorCode & HAL_FDCAN_ERROR_FIFO_EMPTY)
        {
          // FIFO exhausted
          break;
        }
        Error_Handler();
      }
      else if (!q_full)
      {
        q_head = (q_head + 1) % MESSAGE_QUEUE_SIZE;
        q_full = q_head == q_tail;
      }
    }
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  const char *message = "An error encountered!!\r\n";
  HAL_UART_Transmit(&huart1, (const uint8_t *)message, strlen(message), 1000);
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
