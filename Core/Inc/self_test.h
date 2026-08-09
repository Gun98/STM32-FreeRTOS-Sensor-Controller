#ifndef SELF_TEST_H
#define SELF_TEST_H

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "stream_buffer.h"
#include "stm32f4xx_hal.h"

typedef struct
{
    osThreadId_t app_task;
    osThreadId_t heartbeat_task;
    osThreadId_t consumer_task;
    osThreadId_t event_task;
    osThreadId_t monitor_task;
    osThreadId_t command_task;
    osThreadId_t watchdog_task;
    osThreadId_t uart_tx_task;

    osMessageQueueId_t counter_queue;
    osMessageQueueId_t control_queue;
    osMessageQueueId_t uart_tx_queue;
    StreamBufferHandle_t uart_rx_stream_buffer;
    osTimerId_t status_timer;
    osEventFlagsId_t system_event;
    osEventFlagsId_t health_event;
} SelfTestRtosObjects_t;

void SelfTest_RunStartup(
    I2C_HandleTypeDef *i2c,
    UART_HandleTypeDef *uart,
    uint8_t *rtc_hour,
    uint8_t *rtc_minute,
    uint8_t *rtc_second,
    uint8_t *rtc_ok);

uint8_t SelfTest_Init(
    I2C_HandleTypeDef *i2c,
    const SelfTestRtosObjects_t *rtos_objects);

void SelfTest_Run(void);

#endif /* SELF_TEST_H */
