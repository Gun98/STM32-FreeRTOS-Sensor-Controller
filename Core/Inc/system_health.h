#ifndef SYSTEM_HEALTH_H
#define SYSTEM_HEALTH_H

#include "cmsis_os2.h"
#include "stm32f4xx_hal.h"

#define HEALTH_APP       (1UL << 0)
#define HEALTH_SENSOR    (1UL << 1)
#define HEALTH_MONITOR   (1UL << 2)

#define HEALTH_ALL \
    (HEALTH_APP | HEALTH_SENSOR | HEALTH_MONITOR)

typedef struct
{
    IWDG_HandleTypeDef *iwdg;
    osEventFlagsId_t health_event;

    osThreadId_t app_task;
    osThreadId_t heartbeat_task;
    osThreadId_t consumer_task;
    osThreadId_t event_task;
    osThreadId_t monitor_task;
    osThreadId_t command_task;
    osThreadId_t watchdog_task;
    osThreadId_t uart_tx_task;
} SystemHealthContext_t;

void SystemHealth_CaptureResetCause(
    uint32_t reset_flags);

void SystemHealth_Init(
    const SystemHealthContext_t *context);

void SystemHealth_Report(
    uint32_t health_bit);

void WatchdogTask_Run(void *argument);

void SystemHealth_PrintResetCause(void);
void SystemHealth_PrintMemoryStatus(void);
void SystemHealth_TriggerWatchdogFault(void);

void SystemHealth_OnStackOverflow(void);
void SystemHealth_OnMallocFailed(void);

void SystemHealth_CaptureHardFault(
    uint32_t *fault_stack,
    uint32_t exc_return);

#endif /* SYSTEM_HEALTH_H */
