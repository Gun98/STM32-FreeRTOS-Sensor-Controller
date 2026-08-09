#include "uart_tx.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

static UART_HandleTypeDef *uart_tx_uart = NULL;
static osMessageQueueId_t uart_tx_queue = NULL;
static TaskHandle_t uart_tx_task = NULL;


void UartTx_Init(
    UART_HandleTypeDef *uart,
    osMessageQueueId_t tx_queue,
    osThreadId_t tx_task)
{
    uart_tx_uart = uart;
    uart_tx_queue = tx_queue;
    uart_tx_task = (TaskHandle_t)tx_task;
}


HAL_StatusTypeDef UartTx_QueueString(
    const char *text)
{
    size_t length;

    if (text == NULL)
    {
        return HAL_ERROR;
    }

    length = strlen(text);

    if (length >= UART_TX_MESSAGE_SIZE)
    {
        length = UART_TX_MESSAGE_SIZE - 1U;
    }

    if (length == 0U)
    {
        return HAL_ERROR;
    }

    return UartTx_QueueData(
        (const uint8_t *)text,
        (uint16_t)length);
}


HAL_StatusTypeDef UartTx_QueueData(
    const uint8_t *data,
    uint16_t length)
{
    UartTxMessage_t tx_message = {0};

    if ((data == NULL) ||
        (length == 0U) ||
        (length >= UART_TX_MESSAGE_SIZE) ||
        (uart_tx_queue == NULL))
    {
        return HAL_ERROR;
    }

    (void)memcpy(
        tx_message.data,
        data,
        length);

    tx_message.length = length;

    if (osMessageQueuePut(
            uart_tx_queue,
            &tx_message,
            0U,
            20U) != osOK)
    {
        return HAL_BUSY;
    }

    return HAL_OK;
}


void UartTxTask_Run(void *argument)
{
    UartTxMessage_t tx_message = {0};

    (void)argument;

    for (;;)
    {
        if (osMessageQueueGet(
                uart_tx_queue,
                &tx_message,
                NULL,
                osWaitForever) != osOK)
        {
            continue;
        }

        if ((tx_message.length == 0U) ||
            (tx_message.length >= UART_TX_MESSAGE_SIZE))
        {
            continue;
        }

        (void)ulTaskNotifyTake(
            pdTRUE,
            0U);

        if (HAL_UART_Transmit_DMA(
                uart_tx_uart,
                (uint8_t *)tx_message.data,
                tx_message.length) != HAL_OK)
        {
            (void)HAL_UART_AbortTransmit(
                uart_tx_uart);
            continue;
        }

        if (ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(1000U)) == 0U)
        {
            (void)HAL_UART_AbortTransmit(
                uart_tx_uart);
        }
    }
}


void UartTx_OnTransmitComplete(
    UART_HandleTypeDef *uart)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((uart == NULL) ||
        (uart_tx_uart == NULL) ||
        (uart->Instance != uart_tx_uart->Instance) ||
        (uart_tx_task == NULL))
    {
        return;
    }

    vTaskNotifyGiveFromISR(
        uart_tx_task,
        &higher_priority_task_woken);

    portYIELD_FROM_ISR(
        higher_priority_task_woken);
}
