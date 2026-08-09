#include "self_test.h"

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_types.h"
#include "ds3231.h"
#include "uart_tx.h"

#define SELF_TEST_OLED_ADDRESS      0x3CU
#define SELF_TEST_EEPROM_ADDRESS    0x57U
#define SELF_TEST_RTC_ADDRESS       0x68U

#define SELF_TEST_I2C_TRIALS        2U
#define SELF_TEST_I2C_TIMEOUT_MS    20U

typedef struct
{
    uint8_t rtos_objects_ok;
    uint8_t oled_ok;
    uint8_t eeprom_ok;
    uint8_t rtc_ok;
    uint8_t sensor_ready;
} SystemSelfTest_t;

static I2C_HandleTypeDef *self_test_i2c = NULL;
static SelfTestRtosObjects_t self_test_rtos_objects = {0};

static uint8_t SelfTest_RtosObjectsAreReady(void);


void SelfTest_RunStartup(
    I2C_HandleTypeDef *i2c,
    UART_HandleTypeDef *uart,
    uint8_t *rtc_hour,
    uint8_t *rtc_minute,
    uint8_t *rtc_second,
    uint8_t *rtc_ok)
{
    HAL_StatusTypeDef oled_ready_status;
    HAL_StatusTypeDef rtc_ready_status;
    HAL_StatusTypeDef rtc_read_status;
    uint8_t test_hour = 0U;
    uint8_t test_minute = 0U;
    uint8_t test_second = 0U;
    char buffer[160];

    oled_ready_status =
        HAL_I2C_IsDeviceReady(
            i2c,
            (0x3CU << 1),
            2U,
            20U);

    rtc_ready_status =
        HAL_I2C_IsDeviceReady(
            i2c,
            (0x68U << 1),
            2U,
            20U);

    rtc_read_status =
        DS3231_ReadTime(
            &test_hour,
            &test_minute,
            &test_second);

    if (rtc_read_status == HAL_OK)
    {
        *rtc_hour = test_hour;
        *rtc_minute = test_minute;
        *rtc_second = test_second;
        *rtc_ok = 1U;
    }
    else
    {
        *rtc_ok = 0U;
    }

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "I2C TEST OLED=%d RTC=%d READ=%d "
        "ERR=0x%08lX TIME=%02u:%02u:%02u\r\n",
        (int)oled_ready_status,
        (int)rtc_ready_status,
        (int)rtc_read_status,
        (unsigned long)HAL_I2C_GetError(i2c),
        test_hour,
        test_minute,
        test_second);

    (void)HAL_UART_Transmit(
        uart,
        (uint8_t *)buffer,
        strlen(buffer),
        100U);
}


uint8_t SelfTest_Init(
    I2C_HandleTypeDef *i2c,
    const SelfTestRtosObjects_t *rtos_objects)
{
    self_test_i2c = i2c;

    if (rtos_objects == NULL)
    {
        (void)memset(
            &self_test_rtos_objects,
            0,
            sizeof(self_test_rtos_objects));
    }
    else
    {
        self_test_rtos_objects = *rtos_objects;
    }

    return SelfTest_RtosObjectsAreReady();
}


static uint8_t SelfTest_RtosObjectsAreReady(void)
{
    if ((self_test_rtos_objects.app_task == NULL) ||
        (self_test_rtos_objects.heartbeat_task == NULL) ||
        (self_test_rtos_objects.consumer_task == NULL) ||
        (self_test_rtos_objects.event_task == NULL) ||
        (self_test_rtos_objects.monitor_task == NULL) ||
        (self_test_rtos_objects.command_task == NULL) ||
        (self_test_rtos_objects.watchdog_task == NULL) ||
        (self_test_rtos_objects.uart_tx_task == NULL) ||
        (self_test_rtos_objects.counter_queue == NULL) ||
        (self_test_rtos_objects.control_queue == NULL) ||
        (self_test_rtos_objects.uart_tx_queue == NULL) ||
        (self_test_rtos_objects.uart_rx_stream_buffer == NULL) ||
        (self_test_rtos_objects.status_timer == NULL) ||
        (self_test_rtos_objects.system_event == NULL) ||
        (self_test_rtos_objects.health_event == NULL))
    {
        return 0U;
    }

    return 1U;
}


static uint8_t SelfTest_I2cReady(
    uint8_t address_7bit)
{
    HAL_StatusTypeDef status;

    status =
        HAL_I2C_IsDeviceReady(
            self_test_i2c,
            (uint16_t)((uint16_t)address_7bit << 1U),
            SELF_TEST_I2C_TRIALS,
            SELF_TEST_I2C_TIMEOUT_MS);

    return (status == HAL_OK) ? 1U : 0U;
}


static void SelfTest_Collect(
    SystemSelfTest_t *result)
{
    SensorMessage_t sensor_snapshot = {0};

    if (result == NULL)
    {
        return;
    }

    (void)memset(
        result,
        0,
        sizeof(*result));

    result->rtos_objects_ok =
        SelfTest_RtosObjectsAreReady();
    result->oled_ok =
        SelfTest_I2cReady(SELF_TEST_OLED_ADDRESS);
    result->eeprom_ok =
        SelfTest_I2cReady(SELF_TEST_EEPROM_ADDRESS);
    result->rtc_ok =
        SelfTest_I2cReady(SELF_TEST_RTC_ADDRESS);

    if ((App_GetSensorSnapshot(&sensor_snapshot) != 0U) &&
        (sensor_snapshot.valid != 0U))
    {
        result->sensor_ready = 1U;
    }
}


static void SelfTest_Print(
    const SystemSelfTest_t *result)
{
    char buffer[96];
    uint8_t degraded = 0U;

    if (result == NULL)
    {
        return;
    }

    (void)UartTx_QueueString(
        "\r\n=== SYSTEM SELF TEST ===\r\n");

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "[SELF TEST] RTOS OBJECTS : %s\r\n",
        (result->rtos_objects_ok != 0U) ? "PASS" : "FAIL");
    (void)UartTx_QueueString(buffer);

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "[SELF TEST] OLED 0x3C    : %s\r\n",
        (result->oled_ok != 0U) ? "PASS" : "FAIL");
    (void)UartTx_QueueString(buffer);

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "[SELF TEST] EEPROM 0x57  : %s\r\n",
        (result->eeprom_ok != 0U) ? "PASS" : "FAIL");
    (void)UartTx_QueueString(buffer);

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "[SELF TEST] RTC 0x68     : %s\r\n",
        (result->rtc_ok != 0U) ? "PASS" : "FAIL");
    (void)UartTx_QueueString(buffer);

    (void)snprintf(
        buffer,
        sizeof(buffer),
        "[SELF TEST] SENSOR       : %s\r\n",
        (result->sensor_ready != 0U) ? "PASS" : "WAIT");
    (void)UartTx_QueueString(buffer);

    if (result->rtos_objects_ok == 0U)
    {
        (void)UartTx_QueueString(
            "[SELF TEST] RESULT       : FAIL\r\n");
        return;
    }

    if ((result->oled_ok == 0U) ||
        (result->eeprom_ok == 0U) ||
        (result->rtc_ok == 0U))
    {
        degraded = 1U;
    }

    if (degraded != 0U)
    {
        (void)UartTx_QueueString(
            "[SELF TEST] RESULT       : DEGRADED\r\n");
    }
    else
    {
        (void)UartTx_QueueString(
            "[SELF TEST] RESULT       : PASS\r\n");
    }

    (void)UartTx_QueueString(
        "========================\r\n");
}


void SelfTest_Run(void)
{
    SystemSelfTest_t result = {0};

    SelfTest_Collect(&result);
    SelfTest_Print(&result);
}
