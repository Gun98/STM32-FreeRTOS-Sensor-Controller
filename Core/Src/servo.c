/*
 * servo.c
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */
#include "servo.h"

#define SERVO_SAFE_PULSE_US          1000U
#define SERVO_CAUTION_PULSE_US       1250U
#define SERVO_WARNING_PULSE_US       1500U
#define SERVO_ERROR_PULSE_US         1000U

static TIM_HandleTypeDef *servo_timer = NULL;
static uint32_t servo_channel = 0U;

static SystemState_t servo_previous_state =
    SYSTEM_SENSOR_ERROR;

static void Servo_SetPulse(uint32_t pulse_us)
{
    if (servo_timer == NULL)
    {
        return;
    }

    __HAL_TIM_SET_COMPARE(
        servo_timer,
        servo_channel,
        pulse_us);
}

void Servo_Init(TIM_HandleTypeDef *htim,
                uint32_t channel)
{
    servo_timer = htim;
    servo_channel = channel;

    servo_previous_state = SYSTEM_SENSOR_ERROR;

    if (servo_timer != NULL)
    {
        HAL_TIM_PWM_Start(
            servo_timer,
            servo_channel);

        Servo_SetPulse(SERVO_ERROR_PULSE_US);
    }
}

void Servo_Update(SystemState_t state)
{
    uint32_t pulse_us;

    if (servo_timer == NULL)
    {
        return;
    }

    if (state == servo_previous_state)
    {
        return;
    }

    servo_previous_state = state;

    switch (state)
    {
        case SYSTEM_SAFE:
            pulse_us = SERVO_SAFE_PULSE_US;
            break;

        case SYSTEM_CAUTION:
            pulse_us = SERVO_CAUTION_PULSE_US;
            break;

        case SYSTEM_WARNING:
            pulse_us = SERVO_WARNING_PULSE_US;
            break;

        case SYSTEM_SENSOR_ERROR:
        default:
            pulse_us = SERVO_ERROR_PULSE_US;
            break;
    }

    Servo_SetPulse(pulse_us);
}

