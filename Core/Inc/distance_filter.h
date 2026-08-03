/*
 * distance_filter.h
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */

#ifndef INC_DISTANCE_FILTER_H_
#define INC_DISTANCE_FILTER_H_

#include <stdint.h>

#define DISTANCE_FILTER_SIZE    5U

typedef struct
{
    uint32_t samples[DISTANCE_FILTER_SIZE];

    uint8_t sample_index;
    uint8_t sample_count;

    uint32_t filtered_value;
} DistanceFilter_t;

void DistanceFilter_Init(DistanceFilter_t *filter);

uint32_t DistanceFilter_AddSample(
    DistanceFilter_t *filter,
    uint32_t sample);

uint32_t DistanceFilter_GetValue(
    const DistanceFilter_t *filter);

uint8_t DistanceFilter_IsReady(
    const DistanceFilter_t *filter);



#endif /* INC_DISTANCE_FILTER_H_ */
