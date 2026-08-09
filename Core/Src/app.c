/*
 * app.c
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */

#include "app.h"

#include <stdio.h>

#include "app_types.h"
#include "buzzer.h"
#include "servo.h"
#include "ds3231.h"
#include "hcsr04.h"
#include "distance_filter.h"
#include "ssd1306.h"
#include "fonts.h"
#include "self_test.h"
#include "uart_tx.h"

#define TASK_SENSOR_PERIOD_MS          200U
#define TASK_RTC_PERIOD_MS            1000U
#define TASK_DISPLAY_PERIOD_MS         200U
#define TASK_LOGGER_PERIOD_MS         1000U

#define SENSOR_ERROR_LIMIT               3U

#define DISTANCE_MIN_TENTH_CM            20U
#define DISTANCE_MAX_TENTH_CM          4000U

#define WARNING_ENTER_TENTH_CM           90U
#define WARNING_EXIT_TENTH_CM           110U

#define CAUTION_ENTER_TENTH_CM          190U
#define CAUTION_EXIT_TENTH_CM           210U

static SystemState_t system_state = SYSTEM_SENSOR_ERROR;

static uint32_t raw_distance_tenth_cm = 0U;
static uint32_t distance_tenth_cm = 0U;
static uint8_t distance_ok = 0U;
static uint8_t sensor_error_count = 0U;

static uint8_t rtc_hour = 0U;
static uint8_t rtc_minute = 0U;
static uint8_t rtc_second = 0U;
static uint8_t rtc_ok = 0U;

static uint32_t last_trigger_time = 0U;
static uint32_t last_rtc_time = 0U;
static uint32_t last_oled_time = 0U;
static uint32_t last_logger_time = 0U;

static DistanceFilter_t distance_filter;
static uint8_t app_initialized = 0U;



static const char *SystemState_ToString(SystemState_t state);
static void Sensor_RegisterFailure(void);
static void SystemState_UpdateWithHysteresis(uint32_t distance);
static void Sensor_Task(uint32_t now);
static void RTC_Task(uint32_t now);
static void Display_Task(uint32_t now);
static void Logger_Task(uint32_t now);
static void Scheduler_Run(void);

static const char *SystemState_ToString(SystemState_t state)
{
    switch (state)
    {
        case SYSTEM_SAFE:
            return "SAFE";

        case SYSTEM_CAUTION:
            return "CAUTION";

        case SYSTEM_WARNING:
            return "WARNING";

        case SYSTEM_SENSOR_ERROR:
        default:
            return "SENSOR ERR";
    }
}

static void Sensor_RegisterFailure(void)
{
    if (sensor_error_count < SENSOR_ERROR_LIMIT)
    {
        sensor_error_count++;
    }

    if (sensor_error_count >= SENSOR_ERROR_LIMIT)
    {
        sensor_error_count = SENSOR_ERROR_LIMIT;
        distance_ok = 0U;
        system_state = SYSTEM_SENSOR_ERROR;
    }
}

static void SystemState_UpdateWithHysteresis(uint32_t distance)
{
    switch (system_state)
    {
        case SYSTEM_SAFE:
            if (distance <= WARNING_ENTER_TENTH_CM)
            {
                system_state = SYSTEM_WARNING;
            }
            else if (distance <= CAUTION_ENTER_TENTH_CM)
            {
                system_state = SYSTEM_CAUTION;
            }
            break;

        case SYSTEM_CAUTION:
            if (distance <= WARNING_ENTER_TENTH_CM)
            {
                system_state = SYSTEM_WARNING;
            }
            else if (distance >= CAUTION_EXIT_TENTH_CM)
            {
                system_state = SYSTEM_SAFE;
            }
            break;

        case SYSTEM_WARNING:
            if (distance >= CAUTION_EXIT_TENTH_CM)
            {
                system_state = SYSTEM_SAFE;
            }
            else if (distance >= WARNING_EXIT_TENTH_CM)
            {
                system_state = SYSTEM_CAUTION;
            }
            break;

        case SYSTEM_SENSOR_ERROR:
        default:
            if (distance <= WARNING_ENTER_TENTH_CM)
            {
                system_state = SYSTEM_WARNING;
            }
            else if (distance <= CAUTION_ENTER_TENTH_CM)
            {
                system_state = SYSTEM_CAUTION;
            }
            else
            {
                system_state = SYSTEM_SAFE;
            }
            break;
    }
}

static void Sensor_Task(uint32_t now)
{
    uint32_t captured_echo_time = 0U;
    HCSR04_Event_t event;

    HCSR04_Update(now);



    event = HCSR04_GetEvent(&captured_echo_time);

    if (event == HCSR04_EVENT_MEASUREMENT_DONE)
    {


        raw_distance_tenth_cm =
            (captured_echo_time * 10U) / 58U;

        if ((raw_distance_tenth_cm >= DISTANCE_MIN_TENTH_CM) &&
            (raw_distance_tenth_cm <= DISTANCE_MAX_TENTH_CM))
        {
            sensor_error_count = 0U;

            distance_tenth_cm =
                DistanceFilter_AddSample(
                    &distance_filter,
                    raw_distance_tenth_cm);

            distance_ok = 1U;

            SystemState_UpdateWithHysteresis(
                distance_tenth_cm);
        }
        else
        {
            Sensor_RegisterFailure();
        }
    }
    else if (event == HCSR04_EVENT_TIMEOUT)
    {

        Sensor_RegisterFailure();
    }

    if (((now - last_trigger_time) >= TASK_SENSOR_PERIOD_MS) &&
        (HCSR04_IsBusy() == 0U))
    {
        last_trigger_time = now;



        HCSR04_StartMeasurement(now);
    }
}

static void RTC_Task(uint32_t now)
{
    if ((now - last_rtc_time) < TASK_RTC_PERIOD_MS)
    {
        return;
    }

    last_rtc_time = now;

    if (DS3231_ReadTime(
            &rtc_hour,
            &rtc_minute,
            &rtc_second) == HAL_OK)
    {
        rtc_ok = 1U;
    }
    else
    {
        rtc_ok = 0U;
    }
}

static void Display_Task(uint32_t now)
{
    char oled_line1[24];
    char oled_line2[24];
    char oled_line3[24];
    const char *status_text;

    if ((now - last_oled_time) < TASK_DISPLAY_PERIOD_MS)
    {
        return;
    }

    last_oled_time = now;
    status_text = SystemState_ToString(system_state);

    if (rtc_ok != 0U)
    {
        snprintf(oled_line1,
                 sizeof(oled_line1),
                 "TIME %02u:%02u:%02u",
                 rtc_hour,
                 rtc_minute,
                 rtc_second);
    }
    else
    {
        snprintf(oled_line1,
                 sizeof(oled_line1),
                 "TIME RTC ERROR");
    }

    if (distance_ok != 0U)
    {
        snprintf(oled_line2,
                 sizeof(oled_line2),
                 "DIST %lu.%lu cm",
                 (unsigned long)(distance_tenth_cm / 10U),
                 (unsigned long)(distance_tenth_cm % 10U));
    }
    else
    {
        snprintf(oled_line2,
                 sizeof(oled_line2),
                 "DIST ERROR");
    }

    snprintf(oled_line3,
             sizeof(oled_line3),
             "STAT %s",
             status_text);

    ssd1306_Fill(Black);

    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString(oled_line1, Font_7x10, White);

    ssd1306_SetCursor(0, 18);
    ssd1306_WriteString(oled_line2, Font_7x10, White);

    ssd1306_SetCursor(0, 36);
    ssd1306_WriteString(oled_line3, Font_7x10, White);

    ssd1306_UpdateScreen();
}

static void Logger_Task(uint32_t now)
{
	char buffer[160];
	    const char *state_text;


	    if ((now - last_logger_time) < TASK_LOGGER_PERIOD_MS)
	    {
	        return;
	    }

	    last_logger_time = now;
	    state_text = SystemState_ToString(system_state);

	    if ((rtc_ok != 0U) && (distance_ok != 0U))
	    {
	        snprintf(
	            buffer,
	            sizeof(buffer),
	            "TIME %02u:%02u:%02u | RAW %lu.%lu | "
	            "FILTER %lu.%lu cm | ERR %u/%u | STATE %s\r\n",
	            rtc_hour,
	            rtc_minute,
	            rtc_second,
	            (unsigned long)(raw_distance_tenth_cm / 10U),
	            (unsigned long)(raw_distance_tenth_cm % 10U),
	            (unsigned long)(distance_tenth_cm / 10U),
	            (unsigned long)(distance_tenth_cm % 10U),
	            (unsigned int)sensor_error_count,
	            (unsigned int)SENSOR_ERROR_LIMIT,
	            state_text);
	    }
	    else
	    {
	        snprintf(
	            buffer,
	            sizeof(buffer),
	            "RTC %s | DIST %s | ERR %u/%u | STATE %s\r\n",
	            (rtc_ok != 0U) ? "OK" : "ERROR",
	            (distance_ok != 0U) ? "OK" : "ERROR",
	            (unsigned int)sensor_error_count,
	            (unsigned int)SENSOR_ERROR_LIMIT,
	            state_text);
	    }

	    (void)UartTx_QueueString(buffer);
}

static void Scheduler_Run(void)
{
    uint32_t now = HAL_GetTick();

    Sensor_Task(now);
    RTC_Task(now);
    Display_Task(now);
    Logger_Task(now);

    Buzzer_Update(now, system_state);
    Servo_Update(system_state);
}

void App_Init(const AppHardware_t *hardware)
{
    if ((hardware == NULL) ||
        (hardware->i2c == NULL) ||
        (hardware->uart == NULL) ||
        (hardware->hcsr04_timer == NULL) ||
        (hardware->hcsr04_trig_port == NULL) ||
        (hardware->buzzer_timer == NULL) ||
        (hardware->servo_timer == NULL))
    {
        return;
    }

    system_state = SYSTEM_SENSOR_ERROR;

    raw_distance_tenth_cm = 0U;
    distance_tenth_cm = 0U;
    distance_ok = 0U;
    sensor_error_count = 0U;

    rtc_hour = 0U;
    rtc_minute = 0U;
    rtc_second = 0U;
    rtc_ok = 0U;

    last_trigger_time = 0U;
    last_rtc_time = 0U;
    last_oled_time = 0U;
    last_logger_time = 0U;

    HAL_Delay(500U);

    HCSR04_Init(
        hardware->hcsr04_timer,
        hardware->hcsr04_channel,
        hardware->hcsr04_trig_port,
        hardware->hcsr04_trig_pin);

    DS3231_Init(hardware->i2c);
    DistanceFilter_Init(&distance_filter);

    Buzzer_Init(
        hardware->buzzer_timer,
        hardware->buzzer_channel);

    Servo_Init(
        hardware->servo_timer,
        hardware->servo_channel);

    ssd1306_Init();

    SelfTest_RunStartup(
        hardware->i2c,
        hardware->uart,
        &rtc_hour,
        &rtc_minute,
        &rtc_second,
        &rtc_ok);

    app_initialized = 1U;
}



void App_Run(void)
{
    if (app_initialized == 0U)
    {
        return;
    }

    Scheduler_Run();
}

uint8_t App_GetSensorSnapshot(SensorMessage_t *message)
{
    static uint32_t sequence = 0U;

    if ((message == NULL) || (app_initialized == 0U))
    {
        return 0U;
    }

    sequence++;

    message->sequence = sequence;
    message->tick = HAL_GetTick();
    message->distance_tenth_cm = distance_tenth_cm;
    message->valid = distance_ok;

    return 1U;
}


void HAL_TIM_IC_CaptureCallback(
    TIM_HandleTypeDef *htim)
{
    HCSR04_IC_CaptureCallback(htim);
}


