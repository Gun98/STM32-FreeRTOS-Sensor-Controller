/*
 * hcsr04.c
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */
#include "hcsr04.h"

#define HCSR04_TRIGGER_PULSE_US      10U
#define HCSR04_TIMEOUT_MS            50U

static TIM_HandleTypeDef *hcsr04_timer = NULL;
static uint32_t hcsr04_channel = 0U;

static GPIO_TypeDef *hcsr04_trig_port = NULL;
static uint16_t hcsr04_trig_pin = 0U;

static volatile uint32_t hcsr04_rising_capture = 0U;
static volatile uint32_t hcsr04_falling_capture = 0U;
static volatile uint32_t hcsr04_echo_time_us = 0U;

static volatile uint8_t hcsr04_capture_state = 0U;
static volatile uint8_t hcsr04_measurement_done = 0U;
static volatile uint8_t hcsr04_measurement_active = 0U;
static volatile uint8_t hcsr04_timeout_event = 0U;

static uint32_t hcsr04_trigger_start_time = 0U;

static void HCSR04_TriggerPulse(void)
{
    if ((hcsr04_timer == NULL) ||
        (hcsr04_trig_port == NULL))
    {
        return;
    }

    __HAL_TIM_SET_COUNTER(hcsr04_timer, 0U);

    HAL_GPIO_WritePin(
        hcsr04_trig_port,
        hcsr04_trig_pin,
        GPIO_PIN_SET);

    while (__HAL_TIM_GET_COUNTER(hcsr04_timer) <
           HCSR04_TRIGGER_PULSE_US)
    {
        /* 10us 대기 */
    }

    HAL_GPIO_WritePin(
        hcsr04_trig_port,
        hcsr04_trig_pin,
        GPIO_PIN_RESET);
}

void HCSR04_Init(TIM_HandleTypeDef *htim,
                 uint32_t channel,
                 GPIO_TypeDef *trig_port,
                 uint16_t trig_pin)
{
    hcsr04_timer = htim;
    hcsr04_channel = channel;
    hcsr04_trig_port = trig_port;
    hcsr04_trig_pin = trig_pin;

    hcsr04_rising_capture = 0U;
    hcsr04_falling_capture = 0U;
    hcsr04_echo_time_us = 0U;

    hcsr04_capture_state = 0U;
    hcsr04_measurement_done = 0U;
    hcsr04_measurement_active = 0U;
    hcsr04_timeout_event = 0U;

    if ((hcsr04_timer == NULL) ||
        (hcsr04_trig_port == NULL))
    {
        return;
    }

    HAL_GPIO_WritePin(
        hcsr04_trig_port,
        hcsr04_trig_pin,
        GPIO_PIN_RESET);



    __HAL_TIM_SET_CAPTUREPOLARITY(
        hcsr04_timer,
        hcsr04_channel,
        TIM_INPUTCHANNELPOLARITY_RISING);

    HAL_TIM_IC_Start_IT(
        hcsr04_timer,
        hcsr04_channel);
}

void HCSR04_StartMeasurement(uint32_t now)
{
    if ((hcsr04_timer == NULL) ||
        (hcsr04_measurement_active != 0U))
    {
        return;
    }

    hcsr04_capture_state = 0U;
    hcsr04_measurement_done = 0U;
    hcsr04_timeout_event = 0U;
    hcsr04_measurement_active = 1U;

    hcsr04_trigger_start_time = now;

    __HAL_TIM_SET_CAPTUREPOLARITY(
        hcsr04_timer,
        hcsr04_channel,
        TIM_INPUTCHANNELPOLARITY_RISING);

    HCSR04_TriggerPulse();
}

void HCSR04_Update(uint32_t now)
{
    if ((hcsr04_measurement_active != 0U) &&
        ((now - hcsr04_trigger_start_time) >=
         HCSR04_TIMEOUT_MS))
    {
        hcsr04_measurement_active = 0U;
        hcsr04_measurement_done = 0U;
        hcsr04_capture_state = 0U;
        hcsr04_timeout_event = 1U;

        __HAL_TIM_SET_CAPTUREPOLARITY(
            hcsr04_timer,
            hcsr04_channel,
            TIM_INPUTCHANNELPOLARITY_RISING);
    }
}

HCSR04_Event_t HCSR04_GetEvent(
    uint32_t *echo_time_us)
{
    if (hcsr04_measurement_done != 0U)
    {
        if (echo_time_us == NULL)
        {
            return HCSR04_EVENT_NONE;
        }

        *echo_time_us = hcsr04_echo_time_us;
        hcsr04_measurement_done = 0U;

        return HCSR04_EVENT_MEASUREMENT_DONE;
    }

    if (hcsr04_timeout_event != 0U)
    {
        hcsr04_timeout_event = 0U;

        return HCSR04_EVENT_TIMEOUT;
    }

    return HCSR04_EVENT_NONE;
}

uint8_t HCSR04_IsBusy(void)
{
    return hcsr04_measurement_active;
}

void HCSR04_IC_CaptureCallback(
    TIM_HandleTypeDef *htim)
{
    uint32_t timer_period;

    if ((htim != hcsr04_timer) ||
        (hcsr04_measurement_active == 0U))
    {
        return;
    }

    if (hcsr04_capture_state == 0U)
    {
        hcsr04_rising_capture =
            HAL_TIM_ReadCapturedValue(
                hcsr04_timer,
                hcsr04_channel);

        hcsr04_capture_state = 1U;

        __HAL_TIM_SET_CAPTUREPOLARITY(
            hcsr04_timer,
            hcsr04_channel,
            TIM_INPUTCHANNELPOLARITY_FALLING);
    }
    else
    {
        hcsr04_falling_capture =
            HAL_TIM_ReadCapturedValue(
                hcsr04_timer,
                hcsr04_channel);

        timer_period =
            __HAL_TIM_GET_AUTORELOAD(
                hcsr04_timer) + 1U;

        if (hcsr04_falling_capture >=
            hcsr04_rising_capture)
        {
            hcsr04_echo_time_us =
                hcsr04_falling_capture -
                hcsr04_rising_capture;
        }
        else
        {
            hcsr04_echo_time_us =
                (timer_period -
                 hcsr04_rising_capture) +
                hcsr04_falling_capture;
        }

        hcsr04_capture_state = 0U;
        hcsr04_measurement_active = 0U;
        hcsr04_measurement_done = 1U;

        __HAL_TIM_SET_CAPTUREPOLARITY(
            hcsr04_timer,
            hcsr04_channel,
            TIM_INPUTCHANNELPOLARITY_RISING);
    }
}



