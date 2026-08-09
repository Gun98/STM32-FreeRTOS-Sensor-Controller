#include "system_health.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "main.h"
#include "task.h"
#include "uart_tx.h"

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

static SystemHealthContext_t health_context = {0};
static ResetCause_t reset_cause = RESET_CAUSE_UNKNOWN;
static uint32_t reset_csr_raw = 0U;
static volatile HardFaultInfo_t hardfault_info = {0};


static ResetCause_t SystemHealth_DetectResetCause(
    uint32_t reset_flags)
{
    if ((reset_flags & RCC_CSR_IWDGRSTF) != 0U)
    {
        return RESET_CAUSE_IWDG;
    }

    if ((reset_flags & RCC_CSR_WWDGRSTF) != 0U)
    {
        return RESET_CAUSE_WWDG;
    }

    if ((reset_flags & RCC_CSR_SFTRSTF) != 0U)
    {
        return RESET_CAUSE_SOFTWARE;
    }

    if (((reset_flags & RCC_CSR_PORRSTF) != 0U) ||
        ((reset_flags & RCC_CSR_BORRSTF) != 0U))
    {
        return RESET_CAUSE_POWER;
    }

    if ((reset_flags & RCC_CSR_PINRSTF) != 0U)
    {
        return RESET_CAUSE_EXTERNAL_PIN;
    }

    if ((reset_flags & RCC_CSR_LPWRRSTF) != 0U)
    {
        return RESET_CAUSE_LOW_POWER;
    }

    return RESET_CAUSE_UNKNOWN;
}


static const char *SystemHealth_ResetCauseToString(
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


void SystemHealth_CaptureResetCause(
    uint32_t reset_flags)
{
    reset_csr_raw = reset_flags;
    reset_cause =
        SystemHealth_DetectResetCause(
            reset_flags);
}


void SystemHealth_Init(
    const SystemHealthContext_t *context)
{
    if (context == NULL)
    {
        (void)memset(
            &health_context,
            0,
            sizeof(health_context));
    }
    else
    {
        health_context = *context;
    }
}


void SystemHealth_Report(
    uint32_t health_bit)
{
    if (health_context.health_event != NULL)
    {
        (void)osEventFlagsSet(
            health_context.health_event,
            health_bit);
    }
}


void SystemHealth_PrintResetCause(void)
{
    char buffer[128];

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "\r\n[BOOT] RESET CAUSE: %s"
        " | RCC_CSR=0x%08lX\r\n"
        "[CMD] READY - TYPE HELP\r\n",
        SystemHealth_ResetCauseToString(reset_cause),
        (unsigned long)reset_csr_raw);

    (void)UartTx_QueueString(buffer);
}


void SystemHealth_PrintMemoryStatus(void)
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

    free_heap = xPortGetFreeHeapSize();
    minimum_free_heap = xPortGetMinimumEverFreeHeapSize();

    app_stack =
        uxTaskGetStackHighWaterMark(
            (TaskHandle_t)health_context.app_task);
    heartbeat_stack =
        uxTaskGetStackHighWaterMark(
            (TaskHandle_t)health_context.heartbeat_task);
    consumer_stack =
        uxTaskGetStackHighWaterMark(
            (TaskHandle_t)health_context.consumer_task);
    event_stack =
        uxTaskGetStackHighWaterMark(
            (TaskHandle_t)health_context.event_task);
    monitor_stack =
        uxTaskGetStackHighWaterMark(
            (TaskHandle_t)health_context.monitor_task);
    command_stack =
        uxTaskGetStackHighWaterMark(
            (TaskHandle_t)health_context.command_task);
    watchdog_stack =
        uxTaskGetStackHighWaterMark(
            (TaskHandle_t)health_context.watchdog_task);
    uart_tx_stack =
        uxTaskGetStackHighWaterMark(
            (TaskHandle_t)health_context.uart_tx_task);

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "\r\n[MEM] FREE HEAP     : %lu bytes\r\n"
        "[MEM] MIN FREE HEAP : %lu bytes\r\n",
        (unsigned long)free_heap,
        (unsigned long)minimum_free_heap);
    (void)UartTx_QueueString(buffer);

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
    (void)UartTx_QueueString(buffer);

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
    (void)UartTx_QueueString(buffer);

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "[MEM] WATCHDOG  : %lu W / %lu B\r\n"
        "[MEM] UART TX   : %lu W / %lu B\r\n",
        (unsigned long)watchdog_stack,
        (unsigned long)(watchdog_stack * sizeof(StackType_t)),
        (unsigned long)uart_tx_stack,
        (unsigned long)(uart_tx_stack * sizeof(StackType_t)));
    (void)UartTx_QueueString(buffer);
}


void SystemHealth_TriggerWatchdogFault(void)
{
    (void)UartTx_QueueString(
        "[FAULT TEST] APP TASK SUSPEND\r\n"
        "[FAULT TEST] WAIT FOR IWDG RESET\r\n");

    osDelay(200U);

    vTaskSuspend(
        (TaskHandle_t)health_context.app_task);

    if (health_context.health_event != NULL)
    {
        (void)osEventFlagsClear(
            health_context.health_event,
            HEALTH_APP);
    }
}


void WatchdogTask_Run(void *argument)
{
    uint32_t health_flags;
    uint32_t health_timeout_ticks;
    uint32_t idle_delay_ticks;

    (void)argument;

    health_timeout_ticks = osKernelGetTickFreq() * 2U;
    idle_delay_ticks = osKernelGetTickFreq();

    if (health_timeout_ticks == 0U)
    {
        health_timeout_ticks = 2000U;
    }

    if (idle_delay_ticks == 0U)
    {
        idle_delay_ticks = 1000U;
    }

    if (HAL_IWDG_Refresh(health_context.iwdg) != HAL_OK)
    {
        Error_Handler();
    }

    for (;;)
    {
        health_flags =
            osEventFlagsWait(
                health_context.health_event,
                HEALTH_ALL,
                osFlagsWaitAll,
                health_timeout_ticks);

        if ((health_flags & osFlagsError) == 0U)
        {
            if (HAL_IWDG_Refresh(health_context.iwdg) != HAL_OK)
            {
                Error_Handler();
            }
        }
        else
        {
            for (;;)
            {
                osDelay(idle_delay_ticks);
            }
        }
    }
}


void SystemHealth_OnStackOverflow(void)
{
    __disable_irq();

    for (;;)
    {
    }
}


void SystemHealth_OnMallocFailed(void)
{
    __disable_irq();

    for (;;)
    {
    }
}


void SystemHealth_CaptureHardFault(
    uint32_t *fault_stack,
    uint32_t exc_return)
{
    hardfault_info.r0 = fault_stack[0];
    hardfault_info.r1 = fault_stack[1];
    hardfault_info.r2 = fault_stack[2];
    hardfault_info.r3 = fault_stack[3];
    hardfault_info.r12 = fault_stack[4];
    hardfault_info.lr = fault_stack[5];
    hardfault_info.pc = fault_stack[6];
    hardfault_info.xpsr = fault_stack[7];
    hardfault_info.exc_return = exc_return;
    hardfault_info.cfsr = SCB->CFSR;
    hardfault_info.hfsr = SCB->HFSR;
    hardfault_info.dfsr = SCB->DFSR;
    hardfault_info.afsr = SCB->AFSR;
    hardfault_info.bfar = SCB->BFAR;
    hardfault_info.mmfar = SCB->MMFAR;
    hardfault_info.shcsr = SCB->SHCSR;

    __disable_irq();

    if ((CoreDebug->DHCSR &
         CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
    {
        __BKPT(0);
    }

    for (;;)
    {
    }
}
