/*
 * uart_protocol.h
 *
 *  Created on: 2026. 7. 29.
 *      Author: ACER
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>

/*
 * Packet Header
 */
#define UART_PROTOCOL_SOF1                  0xAAU
#define UART_PROTOCOL_SOF2                  0x55U
#define UART_PROTOCOL_VERSION               0x01U
#define UART_PROTOCOL_MAX_PAYLOAD_SIZE      32U

/*
 * Request Packet Type
 */
#define UART_PACKET_TYPE_PING               0x01U
#define UART_PACKET_TYPE_GET_STATUS         0x02U
#define UART_PACKET_TYPE_LED_SET            0x03U

/*
 * Response Packet Type
 */
#define UART_PACKET_TYPE_PONG               0x81U
#define UART_PACKET_TYPE_STATUS             0x82U
#define UART_PACKET_TYPE_ACK                0x83U
#define UART_PACKET_TYPE_ERROR              0xFFU

#define UART_PROTOCOL_HEADER_SIZE 6U
#define UART_PROTOCOL_CRC_SIZE    2U

#define UART_PROTOCOL_MAX_FRAME_SIZE           \
    (UART_PROTOCOL_HEADER_SIZE +                \
     UART_PROTOCOL_MAX_PAYLOAD_SIZE +           \
     UART_PROTOCOL_CRC_SIZE)

#define UART_PROTOCOL_RX_TIMEOUT_MS 100U

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

    uint8_t payload[
        UART_PROTOCOL_MAX_PAYLOAD_SIZE];

} UartProtocolPacket_t;

/*
 * 완성된 Packet을 상위 애플리케이션에 전달하는 Callback.
 *
 * packet 포인터는 Callback 실행 중에만 유효하다.
 */
typedef void (*UartProtocolPacketCallback_t)(
    const UartProtocolPacket_t *packet);

/*
 * Parser 초기화 및 Packet Callback 등록.
 */
void UartProtocol_Init(
    UartProtocolPacketCallback_t callback);

/*
 * 수신 Byte 한 개를 Parser에 전달한다.
 *
 * 반환값:
 * 1U → Binary Packet Parser가 해당 Byte를 소비함
 * 0U → Binary Packet Byte가 아니므로 Text Parser로 전달 가능
 */
uint8_t UartProtocol_ProcessByte(
    uint8_t byte,
    uint32_t now_ms);
/*
 * 진행 중인 Packet을 폐기하고 초기 상태로 복귀한다.
 */
void UartProtocol_Reset(void);

/*
 * Binary Packet 수신 진행 여부를 반환한다.
 */
uint8_t UartProtocol_IsActive(void);

#endif

/*
 * Packet 구조체를 UART 송신용 Byte 배열로 변환한다.
 *
 * 반환값:
 * 0U보다 큼 → 완성된 Frame 길이
 * 0U         → 인수 또는 길이 오류
 */
uint16_t UartProtocol_EncodePacket(
    const UartProtocolPacket_t *packet,
    uint8_t *frame_buffer,
    uint16_t frame_capacity);

uint16_t UartProtocol_CalculateCrc(
    const uint8_t *data,
    uint16_t length);

uint32_t UartProtocol_GetCrcErrorCount(void);


uint8_t UartProtocol_CheckTimeout(
    uint32_t now_ms);

uint32_t UartProtocol_GetTimeoutErrorCount(void);
