/*
 * uart_protocol.c
 *
 *  Created on: 2026. 7. 29.
 *      Author: ACER
 */
#include "uart_protocol.h"

#include <string.h>

typedef enum
{
	    UART_PARSER_WAIT_SOF1 = 0,
	    UART_PARSER_WAIT_SOF2,
	    UART_PARSER_READ_VERSION,
	    UART_PARSER_READ_TYPE,
	    UART_PARSER_READ_SEQUENCE,
	    UART_PARSER_READ_LENGTH,
	    UART_PARSER_READ_PAYLOAD,
	    UART_PARSER_READ_CRC_LOW,
	    UART_PARSER_READ_CRC_HIGH

} UartParserState_t;

static UartParserState_t parser_state =
    UART_PARSER_WAIT_SOF1;

static UartProtocolPacket_t received_packet =
{
    0
};

static uint8_t payload_index = 0U;

static uint8_t received_crc_low = 0U;

static uint32_t crc_error_count = 0U;

static uint32_t parser_last_byte_time_ms = 0U;

static uint32_t timeout_error_count = 0U;

static UartProtocolPacketCallback_t
    packet_callback = NULL;


/*
 * Parser 상태와 Packet 저장 공간을 초기화한다.
 */
static void UartProtocol_ResetInternal(void)
{
    parser_state =
        UART_PARSER_WAIT_SOF1;

    payload_index = 0U;
    received_crc_low = 0U;
    parser_last_byte_time_ms = 0U;

    (void)memset(
        &received_packet,
        0,
        sizeof(received_packet));
}


/*
 * Packet이 완성되면 등록된 Callback으로 전달한다.
 */
static void UartProtocol_DeliverPacket(void)
{
    if (packet_callback != NULL)
    {
        packet_callback(
            &received_packet);
    }

    UartProtocol_ResetInternal();
}


void UartProtocol_Init(
    UartProtocolPacketCallback_t callback)
{
    packet_callback = callback;

    crc_error_count = 0U;
    timeout_error_count = 0U;

    UartProtocol_ResetInternal();
}


void UartProtocol_Reset(void)
{
    UartProtocol_ResetInternal();
}


uint8_t UartProtocol_IsActive(void)
{
    return (parser_state !=
            UART_PARSER_WAIT_SOF1)
        ? 1U
        : 0U;
}
static uint16_t
UartProtocol_CalculateReceivedPacketCrc(void)
{
    uint8_t crc_data[
        4U + UART_PROTOCOL_MAX_PAYLOAD_SIZE] =
    {
        0
    };

    uint16_t crc_data_length;

    /*
     * CRC 대상:
     * VERSION + TYPE + SEQUENCE + LENGTH
     */
    crc_data[0] =
        received_packet.version;

    crc_data[1] =
        received_packet.type;

    crc_data[2] =
        received_packet.sequence;

    crc_data[3] =
        received_packet.length;

    /*
     * Payload가 있다면 Header 정보 뒤에 복사한다.
     */
    if (received_packet.length > 0U)
    {
        (void)memcpy(
            &crc_data[4],
            received_packet.payload,
            received_packet.length);
    }

    crc_data_length =
        (uint16_t)(
            4U +
            received_packet.length);

    return UartProtocol_CalculateCrc(
        crc_data,
        crc_data_length);
}

uint8_t UartProtocol_CheckTimeout(
    uint32_t now_ms)
{
    /*
     * Packet을 받고 있지 않으면
     * Timeout 검사 대상이 아니다.
     */
    if (parser_state ==
        UART_PARSER_WAIT_SOF1)
    {
        return 0U;
    }

    /*
     * uint32_t 뺄셈 방식은 Tick Overflow가 발생해도
     * 경과 시간을 안전하게 비교할 수 있다.
     */
    if ((uint32_t)(
            now_ms -
            parser_last_byte_time_ms) >=
        UART_PROTOCOL_RX_TIMEOUT_MS)
    {
        timeout_error_count++;

        UartProtocol_ResetInternal();

        return 1U;
    }

    return 0U;
}

uint32_t UartProtocol_GetTimeoutErrorCount(void)
{
    return timeout_error_count;
}

uint8_t UartProtocol_ProcessByte(
    uint8_t byte,
    uint32_t now_ms)
{
    uint16_t calculated_crc;
    uint16_t received_crc;

    /*
     * 이전 Packet이 이미 Timeout되었다면
     * 먼저 폐기한 후 현재 Byte를 새 입력으로 처리한다.
     */
    (void)UartProtocol_CheckTimeout(
        now_ms);

    /*
     * Binary Packet을 받고 있는 상태이거나
     * 현재 Byte가 새로운 SOF1이면 수신 시각을 갱신한다.
     */
    if ((parser_state !=
         UART_PARSER_WAIT_SOF1) ||
        (byte == UART_PROTOCOL_SOF1))
    {
        parser_last_byte_time_ms =
            now_ms;
    }

    switch (parser_state)
    {
        case UART_PARSER_WAIT_SOF1:

            if (byte == UART_PROTOCOL_SOF1)
            {
                parser_state =
                    UART_PARSER_WAIT_SOF2;

                return 1U;
            }

            return 0U;


        case UART_PARSER_WAIT_SOF2:

            if (byte == UART_PROTOCOL_SOF2)
            {
                parser_state =
                    UART_PARSER_READ_VERSION;

                return 1U;
            }

            if (byte == UART_PROTOCOL_SOF1)
            {
                /*
                 * AA AA 55에서 두 번째 AA를
                 * 새로운 SOF1 후보로 사용한다.
                 */
                parser_state =
                    UART_PARSER_WAIT_SOF2;

                return 1U;
            }

            UartProtocol_ResetInternal();

            return 0U;


        case UART_PARSER_READ_VERSION:

            if (byte != UART_PROTOCOL_VERSION)
            {
                UartProtocol_ResetInternal();

                return 1U;
            }

            received_packet.version = byte;

            parser_state =
                UART_PARSER_READ_TYPE;

            return 1U;


        case UART_PARSER_READ_TYPE:

            received_packet.type = byte;

            parser_state =
                UART_PARSER_READ_SEQUENCE;

            return 1U;


        case UART_PARSER_READ_SEQUENCE:

            received_packet.sequence = byte;

            parser_state =
                UART_PARSER_READ_LENGTH;

            return 1U;


        case UART_PARSER_READ_LENGTH:

            if (byte >
                UART_PROTOCOL_MAX_PAYLOAD_SIZE)
            {
                UartProtocol_ResetInternal();

                return 1U;
            }

            received_packet.length = byte;
            payload_index = 0U;

            /*
             * Payload가 없어도 Packet 완성이 아니다.
             * CRC 2Byte를 추가로 받아야 한다.
             */
            if (received_packet.length == 0U)
            {
                parser_state =
                    UART_PARSER_READ_CRC_LOW;
            }
            else
            {
                parser_state =
                    UART_PARSER_READ_PAYLOAD;
            }

            return 1U;


        case UART_PARSER_READ_PAYLOAD:

            received_packet.payload[
                payload_index] = byte;

            payload_index++;

            if (payload_index >=
                received_packet.length)
            {
                parser_state =
                    UART_PARSER_READ_CRC_LOW;
            }

            return 1U;


        case UART_PARSER_READ_CRC_LOW:

            /*
             * CRC Low Byte를 먼저 받는다.
             */
            received_crc_low = byte;

            parser_state =
                UART_PARSER_READ_CRC_HIGH;

            return 1U;


        case UART_PARSER_READ_CRC_HIGH:

            /*
             * Little Endian CRC 복원:
             *
             * LOW | HIGH << 8
             */
            received_crc =
                (uint16_t)(
                    (uint16_t)received_crc_low |
                    ((uint16_t)byte << 8U));

            calculated_crc =
                UartProtocol_CalculateReceivedPacketCrc();

            if (received_crc ==
                calculated_crc)
            {
                /*
                 * CRC가 일치할 때만
                 * Packet Handler를 호출한다.
                 */
                UartProtocol_DeliverPacket();
            }
            else
            {
                /*
                 * 손상된 Packet은 실행하지 않는다.
                 */
                crc_error_count++;

                UartProtocol_ResetInternal();
            }

            return 1U;


        default:

            UartProtocol_ResetInternal();

            return 0U;
    }
}

uint16_t UartProtocol_EncodePacket(
    const UartProtocolPacket_t *packet,
    uint8_t *frame_buffer,
    uint16_t frame_capacity)
{
    uint16_t frame_length;
    uint16_t crc_data_length;
    uint16_t calculated_crc;
    uint16_t crc_low_index;

    if ((packet == NULL) ||
        (frame_buffer == NULL))
    {
        return 0U;
    }

    if (packet->length >
        UART_PROTOCOL_MAX_PAYLOAD_SIZE)
    {
        return 0U;
    }

    /*
     * Header + Payload + CRC 2Byte
     */
    frame_length =
        (uint16_t)(
            UART_PROTOCOL_HEADER_SIZE +
            packet->length +
            UART_PROTOCOL_CRC_SIZE);

    if (frame_capacity < frame_length)
    {
        return 0U;
    }

    frame_buffer[0] =
        UART_PROTOCOL_SOF1;

    frame_buffer[1] =
        UART_PROTOCOL_SOF2;

    frame_buffer[2] =
        packet->version;

    frame_buffer[3] =
        packet->type;

    frame_buffer[4] =
        packet->sequence;

    frame_buffer[5] =
        packet->length;

    if (packet->length > 0U)
    {
        (void)memcpy(
            &frame_buffer[
                UART_PROTOCOL_HEADER_SIZE],
            packet->payload,
            packet->length);
    }

    /*
     * CRC 계산 대상:
     *
     * VERSION + TYPE + SEQUENCE + LENGTH + PAYLOAD
     *
     * SOF1과 SOF2는 제외한다.
     */
    crc_data_length =
        (uint16_t)(
            (UART_PROTOCOL_HEADER_SIZE - 2U) +
            packet->length);

    calculated_crc =
        UartProtocol_CalculateCrc(
            &frame_buffer[2],
            crc_data_length);

    /*
     * CRC가 저장될 첫 번째 위치:
     *
     * Header 뒤 + Payload 뒤
     */
    crc_low_index =
        (uint16_t)(
            UART_PROTOCOL_HEADER_SIZE +
            packet->length);

    /*
     * Low Byte 먼저 전송한다.
     */
    frame_buffer[crc_low_index] =
        (uint8_t)(
            calculated_crc &
            0x00FFU);

    frame_buffer[crc_low_index + 1U] =
        (uint8_t)(
            (calculated_crc >> 8U) &
            0x00FFU);

    return frame_length;
}



uint16_t UartProtocol_CalculateCrc(
    const uint8_t *data,
    uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t data_index;
    uint8_t bit_index;

    if (data == NULL)
    {
        return 0U;
    }

    for (data_index = 0U;
         data_index < length;
         data_index++)
    {
        /*
         * 현재 Byte를 CRC의 상위 8Bit에 XOR한다.
         */
        crc ^=
            (uint16_t)(
                (uint16_t)data[data_index]
                << 8U);

        /*
         * Byte 하나는 8Bit이므로
         * 각 Bit를 차례로 처리한다.
         */
        for (bit_index = 0U;
             bit_index < 8U;
             bit_index++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc =
                    (uint16_t)(
                        (crc << 1U) ^
                        0x1021U);
            }
            else
            {
                crc =
                    (uint16_t)(
                        crc << 1U);
            }
        }
    }

    return crc;
}


uint32_t UartProtocol_GetCrcErrorCount(void)
{
    return crc_error_count;
}
