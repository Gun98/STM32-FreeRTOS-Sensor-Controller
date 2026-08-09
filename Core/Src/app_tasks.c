#include "app_tasks.h"

#include <string.h>

#include "app.h"
#include "app_types.h"
#include "main.h"
#include "system_health.h"
#include "uart_tx.h"

static AppTasksContext_t app_tasks_context = {0};


void AppTasks_Init(
    const AppTasksContext_t *context)
{
    if (context == NULL)
    {
        (void)memset(
            &app_tasks_context,
            0,
            sizeof(app_tasks_context));
    }
    else
    {
        app_tasks_context = *context;
    }
}


void AppTasks_OnButtonInterrupt(
    uint16_t gpio_pin)
{
    static uint32_t last_button_tick = 0U;

    if (gpio_pin == USER_BUTTON_Pin)
    {
        uint32_t now = HAL_GetTick();

        if ((now - last_button_tick) >= 50U)
        {
            uint8_t control_command = CONTROL_LED_TOGGLE;

            last_button_tick = now;

            if (app_tasks_context.control_queue != NULL)
            {
                (void)osMessageQueuePut(
                    app_tasks_context.control_queue,
                    &control_command,
                    0U,
                    0U);
            }
        }
    }
}


void AppTasks_OnStatusTimer(void)
{
    if (app_tasks_context.system_event != NULL)
    {
        (void)osEventFlagsSet(
            app_tasks_context.system_event,
            EVENT_TIMER_TICK);
    }
}


void AppTask_Run(void *argument)
{
    SensorMessage_t sensor_message = {0};
    osStatus_t queue_status;
    uint32_t last_queue_tick = osKernelGetTickCount();
    uint32_t now;

    (void)argument;

    last_queue_tick = osKernelGetTickCount();

    for (;;)
    {
        App_Run();
        SystemHealth_Report(HEALTH_APP);

        now = osKernelGetTickCount();

        if ((now - last_queue_tick) >= 200U)
        {
            last_queue_tick = now;

            if (App_GetSensorSnapshot(&sensor_message) != 0U)
            {
                queue_status =
                    osMessageQueuePut(
                        app_tasks_context.counter_queue,
                        &sensor_message,
                        0U,
                        0U);

                if (queue_status != osOK)
                {
                    /* Preserve the existing silent queue failure behavior. */
                }
            }
        }

        osDelay(1U);
    }
}


void HeartbeatTask_Run(void *argument)
{
    (void)argument;

    for (;;)
    {
        osDelay(500U);
    }
}


void ConsumerTask_Run(void *argument)
{
    SensorMessage_t received_message = {0};

    (void)argument;

    for (;;)
    {
        if (osMessageQueueGet(
                app_tasks_context.counter_queue,
                &received_message,
                NULL,
                osWaitForever) == osOK)
        {
            if (app_tasks_context.system_event != NULL)
            {
                if (received_message.valid != 0U)
                {
                    (void)osEventFlagsSet(
                        app_tasks_context.system_event,
                        EVENT_SENSOR_VALID);
                }
                else
                {
                    (void)osEventFlagsClear(
                        app_tasks_context.system_event,
                        EVENT_SENSOR_VALID);
                }
            }

            SystemHealth_Report(HEALTH_SENSOR);
        }
    }
}


void EventTask_Run(void *argument)
{
    uint8_t control_command = 0U;

    (void)argument;

    for (;;)
    {
        if (osMessageQueueGet(
                app_tasks_context.control_queue,
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

                    if (app_tasks_context.system_event != NULL)
                    {
                        (void)osEventFlagsSet(
                            app_tasks_context.system_event,
                            EVENT_BUTTON);
                    }

                    (void)UartTx_QueueString(
                        "[EVENT] USER BUTTON PRESSED\r\n");
                    break;

                case CONTROL_LED_ON:
                    HAL_GPIO_WritePin(
                        GPIOA,
                        GPIO_PIN_5,
                        GPIO_PIN_SET);
                    (void)UartTx_QueueString(
                        "OK: LED ON\r\n");
                    break;

                case CONTROL_LED_OFF:
                    HAL_GPIO_WritePin(
                        GPIOA,
                        GPIO_PIN_5,
                        GPIO_PIN_RESET);
                    (void)UartTx_QueueString(
                        "OK: LED OFF\r\n");
                    break;

                default:
                    (void)UartTx_QueueString(
                        "ERR: INVALID CONTROL COMMAND\r\n");
                    break;
            }
        }
    }
}


void MonitorTask_Run(void *argument)
{
    uint32_t flags;
    uint32_t current_flags;
    uint32_t one_second_ticks;

    (void)argument;

    one_second_ticks = osKernelGetTickFreq();

    if (one_second_ticks == 0U)
    {
        one_second_ticks = 1000U;
    }

    (void)osTimerStart(
        app_tasks_context.status_timer,
        one_second_ticks);

    for (;;)
    {
        flags =
            osEventFlagsWait(
                app_tasks_context.system_event,
                EVENT_TIMER_TICK | EVENT_BUTTON,
                osFlagsWaitAny,
                osWaitForever);

        if ((flags & osFlagsError) != 0U)
        {
            continue;
        }

        if ((flags & EVENT_TIMER_TICK) != 0U)
        {
            current_flags =
                osEventFlagsGet(
                    app_tasks_context.system_event);

            if ((current_flags & osFlagsError) != 0U)
            {
                (void)UartTx_QueueString(
                    "[MONITOR] EVENT FLAGS ERROR\r\n");
            }
            else if ((current_flags & EVENT_SENSOR_VALID) != 0U)
            {
                (void)UartTx_QueueString(
                    "[MONITOR] TIMER | SENSOR VALID\r\n");
            }
            else
            {
                (void)UartTx_QueueString(
                    "[MONITOR] TIMER | SENSOR INVALID\r\n");
            }

            SystemHealth_Report(HEALTH_MONITOR);
        }

        if ((flags & EVENT_BUTTON) != 0U)
        {
            (void)UartTx_QueueString(
                "[MONITOR] USER BUTTON EVENT\r\n");
        }
    }
}
