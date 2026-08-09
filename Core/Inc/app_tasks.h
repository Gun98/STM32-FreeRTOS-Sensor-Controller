#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "cmsis_os2.h"
#include "stm32f4xx_hal.h"

#define EVENT_TIMER_TICK      (1UL << 0)
#define EVENT_BUTTON          (1UL << 1)
#define EVENT_SENSOR_VALID    (1UL << 2)

#define CONTROL_LED_TOGGLE    1U
#define CONTROL_LED_ON        2U
#define CONTROL_LED_OFF       3U

typedef struct
{
    osMessageQueueId_t counter_queue;
    osMessageQueueId_t control_queue;
    osEventFlagsId_t system_event;
    osTimerId_t status_timer;
} AppTasksContext_t;

void AppTasks_Init(
    const AppTasksContext_t *context);

void AppTask_Run(void *argument);
void HeartbeatTask_Run(void *argument);
void ConsumerTask_Run(void *argument);
void EventTask_Run(void *argument);
void MonitorTask_Run(void *argument);

void AppTasks_OnButtonInterrupt(
    uint16_t gpio_pin);

void AppTasks_OnStatusTimer(void);

#endif /* APP_TASKS_H */
