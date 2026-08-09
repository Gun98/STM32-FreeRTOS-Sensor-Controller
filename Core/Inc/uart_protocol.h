#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "stream_buffer.h"
#include "stm32f4xx_hal.h"

#define UART_PROTOCOL_SOF1                  0xAAU
#define UART_PROTOCOL_SOF2                  0x55U
#define UART_PROTOCOL_VERSION               0x01U
#define UART_PROTOCOL_MAX_PAYLOAD_SIZE      32U

#define UART_PACKET_TYPE_PING               0x01U
#define UART_PACKET_TYPE_GET_STATUS         0x02U
#define UART_PACKET_TYPE_LED_SET            0x03U

#define UART_PACKET_TYPE_PONG               0x81U
#define UART_PACKET_TYPE_STATUS             0x82U
#define UART_PACKET_TYPE_ACK                0x83U
#define UART_PACKET_TYPE_ERROR              0xFFU

#define UART_PROTOCOL_HEADER_SIZE           6U
#define UART_PROTOCOL_CRC_SIZE              2U

#define UART_PROTOCOL_MAX_FRAME_SIZE           \
    (UART_PROTOCOL_HEADER_SIZE +                \
     UART_PROTOCOL_MAX_PAYLOAD_SIZE +           \
     UART_PROTOCOL_CRC_SIZE)

#define UART_PROTOCOL_RX_TIMEOUT_MS         100U

#define UART_PROTOCOL_RESULT_OK                 0x00U

#define UART_PROTOCOL_ERROR_INVALID_LENGTH      0x01U
#define UART_PROTOCOL_ERROR_INVALID_PAYLOAD     0x02U
#define UART_PROTOCOL_ERROR_UNKNOWN_TYPE        0x03U
#define UART_PROTOCOL_ERROR_SNAPSHOT_FAILED     0x04U
#define UART_PROTOCOL_ERROR_CONTROL_QUEUE_FULL  0x05U

typedef struct
{
    uint8_t version;
    uint8_t type;
    uint8_t sequence;
    uint8_t length;
    uint8_t payload[UART_PROTOCOL_MAX_PAYLOAD_SIZE];
} UartProtocolPacket_t;

typedef struct
{
    UART_HandleTypeDef *uart;
    StreamBufferHandle_t rx_stream_buffer;
    osMessageQueueId_t control_queue;
} UartProtocolContext_t;

typedef struct
{
    uint32_t valid_count;
    uint32_t duplicate_count;
    uint32_t crc_error_count;
    uint32_t timeout_count;
    uint32_t rx_drop_count;
    uint32_t tx_fail_count;
} UartProtocolStats_t;

void UartProtocol_Init(
    const UartProtocolContext_t *context);

void UartProtocol_Reset(void);

uint8_t UartProtocol_IsActive(void);

uint8_t UartProtocol_ProcessByte(
    uint8_t byte,
    uint32_t now_ms);

uint8_t UartProtocol_CheckTimeout(
    uint32_t now_ms);

uint16_t UartProtocol_EncodePacket(
    const UartProtocolPacket_t *packet,
    uint8_t *frame_buffer,
    uint16_t frame_capacity);

uint16_t UartProtocol_CalculateCrc(
    const uint8_t *data,
    uint16_t length);

void UartProtocol_GetStats(
    UartProtocolStats_t *stats);

void UartProtocol_OnReceiveComplete(
    UART_HandleTypeDef *uart);

void CommandTask_Run(void *argument);

#endif /* UART_PROTOCOL_H */
