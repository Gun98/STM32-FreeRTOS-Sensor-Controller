#ifndef UART_TX_H
#define UART_TX_H

#include "app_types.h"
#include "cmsis_os2.h"
#include "stm32f4xx_hal.h"

void UartTx_Init(
    UART_HandleTypeDef *uart,
    osMessageQueueId_t tx_queue,
    osThreadId_t tx_task);

HAL_StatusTypeDef UartTx_QueueString(
    const char *text);

HAL_StatusTypeDef UartTx_QueueData(
    const uint8_t *data,
    uint16_t length);

void UartTxTask_Run(void *argument);

void UartTx_OnTransmitComplete(
    UART_HandleTypeDef *uart);

#endif /* UART_TX_H */
