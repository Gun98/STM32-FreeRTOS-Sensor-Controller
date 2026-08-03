/*
 * distance_filter.c
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */
#include "distance_filter.h"
#include <stddef.h>

static uint32_t DistanceFilter_CalculateMedian(
    const uint32_t samples[DISTANCE_FILTER_SIZE])
{
    uint32_t sorted[DISTANCE_FILTER_SIZE];

    for (uint8_t i = 0U;
         i < DISTANCE_FILTER_SIZE;
         i++)
    {
        sorted[i] = samples[i];
    }

    for (uint8_t i = 0U;
         i < (DISTANCE_FILTER_SIZE - 1U);
         i++)
    {
        for (uint8_t j = 0U;
             j < (DISTANCE_FILTER_SIZE - 1U - i);
             j++)
        {
            if (sorted[j] > sorted[j + 1U])
            {
                uint32_t temp = sorted[j];

                sorted[j] = sorted[j + 1U];
                sorted[j + 1U] = temp;
            }
        }
    }

    return sorted[DISTANCE_FILTER_SIZE / 2U];
}

void DistanceFilter_Init(DistanceFilter_t *filter)
{
    if (filter == NULL)
    {
        return;
    }

    for (uint8_t i = 0U;
         i < DISTANCE_FILTER_SIZE;
         i++)
    {
        filter->samples[i] = 0U;
    }

    filter->sample_index = 0U;
    filter->sample_count = 0U;
    filter->filtered_value = 0U;
}

uint32_t DistanceFilter_AddSample(
    DistanceFilter_t *filter,
    uint32_t sample)
{
    if (filter == NULL)
    {
        return 0U;
    }

    filter->samples[filter->sample_index] = sample;

    filter->sample_index++;

    if (filter->sample_index >= DISTANCE_FILTER_SIZE)
    {
        filter->sample_index = 0U;
    }

    if (filter->sample_count < DISTANCE_FILTER_SIZE)
    {
        filter->sample_count++;
    }

    if (filter->sample_count < DISTANCE_FILTER_SIZE)
    {
        filter->filtered_value = sample;
    }
    else
    {
        filter->filtered_value =
            DistanceFilter_CalculateMedian(
                filter->samples);
    }

    return filter->filtered_value;
}

uint32_t DistanceFilter_GetValue(
    const DistanceFilter_t *filter)
{
    if (filter == NULL)
    {
        return 0U;
    }

    return filter->filtered_value;
}

uint8_t DistanceFilter_IsReady(
    const DistanceFilter_t *filter)
{
    if (filter == NULL)
    {
        return 0U;
    }

    if (filter->sample_count >= DISTANCE_FILTER_SIZE)
    {
        return 1U;
    }

    return 0U;
}

