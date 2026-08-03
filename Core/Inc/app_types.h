/*
 * app_types.h
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */


#ifndef INC_APP_TYPES_H_
#define INC_APP_TYPES_H_
#define UART_TX_MESSAGE_SIZE  160U

#include <stdint.h>

typedef enum
{
    SYSTEM_SAFE = 0,
    SYSTEM_CAUTION,
    SYSTEM_WARNING,
    SYSTEM_SENSOR_ERROR
} SystemState_t;

typedef struct
{
    uint32_t sequence;
    uint32_t tick;
    uint32_t distance_tenth_cm;
    uint8_t valid;
} SensorMessage_t;



typedef struct
{
    uint16_t length;
    char data[UART_TX_MESSAGE_SIZE];
} UartTxMessage_t;

#endif /* INC_APP_TYPES_H_ */
