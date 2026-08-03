/*
 * buzzer.c
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */
#include "buzzer.h"

#define BUZZER_DUTY_TICKS           50U

#define CAUTION_ON_TIME_MS         100U
#define CAUTION_OFF_TIME_MS        900U

#define WARNING_ON_TIME_MS         100U
#define WARNING_OFF_TIME_MS        100U

#define ERROR_ON_TIME_MS           200U
#define ERROR_OFF_TIME_MS          200U

static TIM_HandleTypeDef *buzzer_timer = NULL;
static uint32_t buzzer_channel = 0U;

static uint8_t buzzer_is_on = 0U;
static uint32_t buzzer_last_toggle_time = 0U;

static SystemState_t buzzer_previous_state =
    SYSTEM_SENSOR_ERROR;

static void Buzzer_Set(uint8_t on)
{
    if (buzzer_timer == NULL)
    {
        return;
    }

    if (on != 0U)
    {
        __HAL_TIM_SET_COMPARE(
            buzzer_timer,
            buzzer_channel,
            BUZZER_DUTY_TICKS);
    }
    else
    {
        __HAL_TIM_SET_COMPARE(
            buzzer_timer,
            buzzer_channel,
            0U);
    }
}

void Buzzer_Init(TIM_HandleTypeDef *htim,
                 uint32_t channel)
{
    buzzer_timer = htim;
    buzzer_channel = channel;

    buzzer_is_on = 0U;
    buzzer_last_toggle_time = 0U;
    buzzer_previous_state = SYSTEM_SENSOR_ERROR;

    if (buzzer_timer != NULL)
    {
        HAL_TIM_PWM_Start(
            buzzer_timer,
            buzzer_channel);

        Buzzer_Set(0U);
    }
}

void Buzzer_Update(uint32_t now,
                   SystemState_t state)
{
    uint32_t on_time;
    uint32_t off_time;
    uint32_t current_period;

    if (buzzer_timer == NULL)
    {
        return;
    }

    if (state != buzzer_previous_state)
    {
        buzzer_previous_state = state;
        buzzer_last_toggle_time = now;
        buzzer_is_on = 0U;

        Buzzer_Set(0U);
    }

    switch (state)
    {
        case SYSTEM_SAFE:
            buzzer_is_on = 0U;
            Buzzer_Set(0U);
            return;

        case SYSTEM_CAUTION:
            on_time = CAUTION_ON_TIME_MS;
            off_time = CAUTION_OFF_TIME_MS;
            break;

        case SYSTEM_WARNING:
            on_time = WARNING_ON_TIME_MS;
            off_time = WARNING_OFF_TIME_MS;
            break;

        case SYSTEM_SENSOR_ERROR:
            on_time = ERROR_ON_TIME_MS;
            off_time = ERROR_OFF_TIME_MS;
            break;

        default:
            buzzer_is_on = 0U;
            Buzzer_Set(0U);
            return;
    }

    if (buzzer_is_on != 0U)
    {
        current_period = on_time;
    }
    else
    {
        current_period = off_time;
    }

    if ((now - buzzer_last_toggle_time) >=
        current_period)
    {
        buzzer_last_toggle_time = now;
        buzzer_is_on = !buzzer_is_on;

        Buzzer_Set(buzzer_is_on);
    }
}

