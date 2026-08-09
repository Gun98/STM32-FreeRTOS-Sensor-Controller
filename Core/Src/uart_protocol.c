/*
 * uart_protocol.c
 *
 *  Created on: 2026. 7. 29.
 *      Author: ACER
 */
#include "uart_protocol.h"

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_tasks.h"
#include "FreeRTOS.h"
#include "self_test.h"
#include "system_health.h"
#include "task.h"
#include "uart_tx.h"

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

typedef struct
{
    uint8_t valid;
    UartProtocolPacket_t request;
    uint16_t response_length;
    uint8_t response_frame[UART_PROTOCOL_MAX_FRAME_SIZE];
} UartProtocolTransactionCache_t;

typedef void (*UartProtocolPacketCallback_t)(
    const UartProtocolPacket_t *packet);

static void UartProtocol_OnPacket(
    const UartProtocolPacket_t *packet);

static UartProtocolContext_t protocol_context = {0};
static uint8_t uart_rx_byte = 0U;
static volatile uint32_t uart_rx_drop_count = 0U;

static UartProtocolTransactionCache_t transaction_cache = {0};
static uint32_t valid_packet_count = 0U;
static uint32_t duplicate_request_count = 0U;
static uint32_t tx_queue_failure_count = 0U;

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


static void UartProtocol_ParserInit(
    UartProtocolPacketCallback_t callback)
{
    packet_callback = callback;

    crc_error_count = 0U;
    timeout_error_count = 0U;

    UartProtocol_ResetInternal();
}


void UartProtocol_Init(
    const UartProtocolContext_t *context)
{
    if (context == NULL)
    {
        (void)memset(
            &protocol_context,
            0,
            sizeof(protocol_context));
    }
    else
    {
        protocol_context = *context;
    }

    uart_rx_byte = 0U;
    uart_rx_drop_count = 0U;
    valid_packet_count = 0U;
    duplicate_request_count = 0U;
    tx_queue_failure_count = 0U;
    (void)memset(
        &transaction_cache,
        0,
        sizeof(transaction_cache));

    UartProtocol_ParserInit(
        UartProtocol_OnPacket);
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


void UartProtocol_GetStats(
    UartProtocolStats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->valid_count = valid_packet_count;
    stats->duplicate_count = duplicate_request_count;
    stats->crc_error_count = crc_error_count;
    stats->timeout_count = timeout_error_count;
    stats->rx_drop_count = uart_rx_drop_count;
    stats->tx_fail_count = tx_queue_failure_count;
}


void UartProtocol_OnReceiveComplete(
    UART_HandleTypeDef *uart)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    size_t sent_length = 0U;

    if ((uart == NULL) ||
        (protocol_context.uart == NULL) ||
        (uart->Instance != protocol_context.uart->Instance))
    {
        return;
    }

    if (protocol_context.rx_stream_buffer != NULL)
    {
        sent_length =
            xStreamBufferSendFromISR(
                protocol_context.rx_stream_buffer,
                &uart_rx_byte,
                1U,
                &higher_priority_task_woken);

        if (sent_length != 1U)
        {
            uart_rx_drop_count++;
        }
    }
    else
    {
        uart_rx_drop_count++;
    }

    (void)HAL_UART_Receive_IT(
        protocol_context.uart,
        &uart_rx_byte,
        1U);

    portYIELD_FROM_ISR(
        higher_priority_task_woken);
}


static HAL_StatusTypeDef UartProtocol_QueueFrame(
    const uint8_t *frame,
    uint16_t frame_length)
{
    HAL_StatusTypeDef tx_status;

    if ((frame == NULL) ||
        (frame_length == 0U) ||
        (frame_length > UART_PROTOCOL_MAX_FRAME_SIZE))
    {
        return HAL_ERROR;
    }

    tx_status =
        UartTx_QueueData(
            frame,
            frame_length);

    if (tx_status != HAL_OK)
    {
        tx_queue_failure_count++;
    }

    return tx_status;
}


static uint8_t UartProtocol_IsDuplicateRequest(
    const UartProtocolPacket_t *packet)
{
    if ((packet == NULL) ||
        (transaction_cache.valid == 0U))
    {
        return 0U;
    }

    if ((transaction_cache.request.version != packet->version) ||
        (transaction_cache.request.type != packet->type) ||
        (transaction_cache.request.sequence != packet->sequence) ||
        (transaction_cache.request.length != packet->length) ||
        (packet->length > UART_PROTOCOL_MAX_PAYLOAD_SIZE))
    {
        return 0U;
    }

    if ((packet->length > 0U) &&
        (memcmp(
            transaction_cache.request.payload,
            packet->payload,
            packet->length) != 0))
    {
        return 0U;
    }

    return 1U;
}


static void UartProtocol_StoreTransaction(
    const UartProtocolPacket_t *request,
    const uint8_t *response_frame,
    uint16_t response_length)
{
    if ((request == NULL) ||
        (response_frame == NULL) ||
        (response_length == 0U) ||
        (response_length > UART_PROTOCOL_MAX_FRAME_SIZE))
    {
        return;
    }

    transaction_cache.valid = 0U;
    transaction_cache.request = *request;

    (void)memcpy(
        transaction_cache.response_frame,
        response_frame,
        response_length);

    transaction_cache.response_length = response_length;
    transaction_cache.valid = 1U;
}


static HAL_StatusTypeDef UartProtocol_ResendCachedResponse(void)
{
    if ((transaction_cache.valid == 0U) ||
        (transaction_cache.response_length == 0U) ||
        (transaction_cache.response_length > UART_PROTOCOL_MAX_FRAME_SIZE))
    {
        return HAL_ERROR;
    }

    return UartProtocol_QueueFrame(
        transaction_cache.response_frame,
        transaction_cache.response_length);
}


static HAL_StatusTypeDef UartProtocol_SendResponseAndCache(
    const UartProtocolPacket_t *request,
    const UartProtocolPacket_t *response)
{
    uint8_t response_frame[UART_PROTOCOL_MAX_FRAME_SIZE] = {0};
    uint16_t response_length;

    if ((request == NULL) || (response == NULL))
    {
        return HAL_ERROR;
    }

    response_length =
        UartProtocol_EncodePacket(
            response,
            response_frame,
            sizeof(response_frame));

    if (response_length == 0U)
    {
        return HAL_ERROR;
    }

    UartProtocol_StoreTransaction(
        request,
        response_frame,
        response_length);

    return UartProtocol_QueueFrame(
        response_frame,
        response_length);
}


static void UartProtocol_PrintFrameHex(
    const uint8_t *frame,
    uint16_t frame_length)
{
    static char hex_text[160];
    size_t text_index = 0U;
    uint16_t frame_index;
    int written;

    if ((frame == NULL) || (frame_length == 0U))
    {
        return;
    }

    written =
        snprintf(
            hex_text,
            sizeof(hex_text),
            "[PKT TX HEX] ");

    if (written < 0)
    {
        return;
    }

    text_index = (size_t)written;

    for (frame_index = 0U;
         frame_index < frame_length;
         frame_index++)
    {
        if (text_index >= sizeof(hex_text))
        {
            break;
        }

        written =
            snprintf(
                &hex_text[text_index],
                sizeof(hex_text) - text_index,
                "%02X ",
                (unsigned int)frame[frame_index]);

        if (written < 0)
        {
            return;
        }

        if ((size_t)written >=
            (sizeof(hex_text) - text_index))
        {
            text_index = sizeof(hex_text) - 1U;
            break;
        }

        text_index += (size_t)written;
    }

    if (text_index < (sizeof(hex_text) - 2U))
    {
        hex_text[text_index++] = '\r';
        hex_text[text_index++] = '\n';
        hex_text[text_index] = '\0';
    }
    else
    {
        hex_text[sizeof(hex_text) - 1U] = '\0';
    }

    (void)UartTx_QueueString(hex_text);
}


static void UartProtocol_SendAck(
    const UartProtocolPacket_t *request)
{
    UartProtocolPacket_t response_packet = {0};

    if (request == NULL)
    {
        return;
    }

    response_packet.version = UART_PROTOCOL_VERSION;
    response_packet.type = UART_PACKET_TYPE_ACK;
    response_packet.sequence = request->sequence;
    response_packet.length = 2U;
    response_packet.payload[0] = request->type;
    response_packet.payload[1] = UART_PROTOCOL_RESULT_OK;

    if (UartProtocol_SendResponseAndCache(
            request,
            &response_packet) != HAL_OK)
    {
        (void)UartTx_QueueString(
            "[PKT] ACK TX ERROR\r\n");
    }
}


static void UartProtocol_RunPacketTest(void)
{
    static const uint8_t test_packet[] =
    {
        0xAAU,
        0x55U,
        0x01U,
        0x01U,
        0x2AU,
        0x00U,
        0x69U,
        0x2CU
    };
    size_t test_index;

    for (test_index = 0U;
         test_index < sizeof(test_packet);
         test_index++)
    {
        (void)UartProtocol_ProcessByte(
            test_packet[test_index],
            HAL_GetTick());
    }
}


static void UartProtocol_RunCrcTest(
    char *response_buffer,
    size_t response_size)
{
    static const uint8_t crc_test_data[] =
    {
        0x01U,
        0x01U,
        0x10U,
        0x00U
    };
    uint16_t calculated_crc;

    calculated_crc =
        UartProtocol_CalculateCrc(
            crc_test_data,
            (uint16_t)sizeof(crc_test_data));

    (void)snprintf(
        response_buffer,
        response_size,
        "[CRC TEST] DATA 01 01 10 00 | "
        "CRC 0x%04X | TX %02X %02X | %s\r\n",
        (unsigned int)calculated_crc,
        (unsigned int)(calculated_crc & 0x00FFU),
        (unsigned int)((calculated_crc >> 8U) & 0x00FFU),
        (calculated_crc == 0xC637U) ? "PASS" : "FAIL");

    (void)UartTx_QueueString(response_buffer);
}


static void UartProtocol_RunBadCrcTest(
    char *response_buffer,
    size_t response_size)
{
    static const uint8_t bad_crc_packet[] =
    {
        0xAAU,
        0x55U,
        0x01U,
        0x01U,
        0x2AU,
        0x00U,
        0x69U,
        0x2DU
    };
    uint32_t error_count_before;
    uint32_t error_count_after;
    size_t test_index;

    error_count_before = crc_error_count;

    for (test_index = 0U;
         test_index < sizeof(bad_crc_packet);
         test_index++)
    {
        (void)UartProtocol_ProcessByte(
            bad_crc_packet[test_index],
            HAL_GetTick());
    }

    error_count_after = crc_error_count;

    (void)snprintf(
        response_buffer,
        response_size,
        "[CRC BAD TEST] BEFORE %lu | "
        "AFTER %lu | %s\r\n",
        (unsigned long)error_count_before,
        (unsigned long)error_count_after,
        (error_count_after == (error_count_before + 1U))
            ? "PASS"
            : "FAIL");

    (void)UartTx_QueueString(response_buffer);
}


static void UartProtocol_PrintStats(void)
{
    char response_buffer[128];
    UartProtocolStats_t stats = {0};

    UartProtocol_GetStats(&stats);

    (void)snprintf(
        response_buffer,
        sizeof(response_buffer),
        "[PKT STAT] VALID %lu | "
        "DUPLICATE %lu\r\n",
        (unsigned long)stats.valid_count,
        (unsigned long)stats.duplicate_count);

    (void)UartTx_QueueString(response_buffer);

    (void)snprintf(
        response_buffer,
        sizeof(response_buffer),
        "[PKT STAT] CRC ERROR %lu | "
        "TIMEOUT %lu | RX DROP %lu | "
        "TX FAIL %lu\r\n",
        (unsigned long)stats.crc_error_count,
        (unsigned long)stats.timeout_count,
        (unsigned long)stats.rx_drop_count,
        (unsigned long)stats.tx_fail_count);

    (void)UartTx_QueueString(response_buffer);
}


static void UartProtocol_RunTimeoutTest(
    char *response_buffer,
    size_t response_size)
{
    static const uint8_t partial_packet[] =
    {
        0xAAU,
        0x55U,
        0x01U,
        0x03U
    };
    uint32_t timeout_before;
    uint32_t timeout_after;
    uint8_t timeout_detected;
    size_t test_index;

    timeout_before = timeout_error_count;

    for (test_index = 0U;
         test_index < sizeof(partial_packet);
         test_index++)
    {
        (void)UartProtocol_ProcessByte(
            partial_packet[test_index],
            HAL_GetTick());
    }

    osDelay(150U);

    timeout_detected =
        UartProtocol_CheckTimeout(
            HAL_GetTick());

    timeout_after = timeout_error_count;

    (void)snprintf(
        response_buffer,
        response_size,
        "[TIMEOUT TEST] BEFORE %lu | "
        "AFTER %lu | DETECTED %u | "
        "ACTIVE %u | %s\r\n",
        (unsigned long)timeout_before,
        (unsigned long)timeout_after,
        (unsigned int)timeout_detected,
        (unsigned int)UartProtocol_IsActive(),
        ((timeout_after == (timeout_before + 1U)) &&
         (timeout_detected == 1U) &&
         (UartProtocol_IsActive() == 0U))
            ? "PASS"
            : "FAIL");

    (void)UartTx_QueueString(response_buffer);
}


static void CommandTask_ProcessCommand(
    const char *command_buffer)
{
    char response_buffer[128] = {0};
    SensorMessage_t sensor_snapshot = {0};
    uint8_t control_value = 0U;

    if (strcmp(command_buffer, "HELP") == 0)
    {
        (void)UartTx_QueueString(
            "COMMANDS:\r\n"
            "  HELP\r\n"
            "  STATUS\r\n"
            "  MEM\r\n"
            "  LED ON\r\n"
            "  LED OFF\r\n"
            "  PKT TEST\r\n"
            "  PKT CRC TEST\r\n"
            "  PKT CRC BAD\r\n"
            "  PKT TIMEOUT TEST\r\n"
            "  PKT STAT\r\n"
            "  SELF TEST\r\n"
        #if defined(DEBUG)
            "  FAULT WATCHDOG\r\n"
            "  FAULT STACK\r\n"
            "  FAULT MALLOC\r\n"
            "  FAULT HARD\r\n"
        #endif
        );
    }
    else if (strcmp(command_buffer, "STATUS") == 0)
    {
        if (App_GetSensorSnapshot(&sensor_snapshot) != 0U)
        {
            (void)snprintf(
                response_buffer,
                sizeof(response_buffer),
                "STATUS | SEQ %lu | TICK %lu | "
                "DIST %lu.%lu cm | SENSOR %s\r\n",
                (unsigned long)sensor_snapshot.sequence,
                (unsigned long)sensor_snapshot.tick,
                (unsigned long)(sensor_snapshot.distance_tenth_cm / 10U),
                (unsigned long)(sensor_snapshot.distance_tenth_cm % 10U),
                (sensor_snapshot.valid != 0U)
                    ? "VALID"
                    : "INVALID");

            (void)UartTx_QueueString(response_buffer);
        }
        else
        {
            (void)UartTx_QueueString(
                "STATUS ERROR: SNAPSHOT UNAVAILABLE\r\n");
        }
    }
    else if (strcmp(command_buffer, "MEM") == 0)
    {
        SystemHealth_PrintMemoryStatus();
    }
    else if (strcmp(command_buffer, "PKT TEST") == 0)
    {
        UartProtocol_RunPacketTest();
    }
    else if (strcmp(command_buffer, "PKT CRC TEST") == 0)
    {
        UartProtocol_RunCrcTest(
            response_buffer,
            sizeof(response_buffer));
    }
    else if (strcmp(command_buffer, "SELF TEST") == 0)
    {
        SelfTest_Run();
    }
    else if (strcmp(command_buffer, "PKT CRC BAD") == 0)
    {
        UartProtocol_RunBadCrcTest(
            response_buffer,
            sizeof(response_buffer));
    }
    else if (strcmp(command_buffer, "PKT STAT") == 0)
    {
        UartProtocol_PrintStats();
    }
    else if (strcmp(command_buffer, "PKT TIMEOUT TEST") == 0)
    {
        UartProtocol_RunTimeoutTest(
            response_buffer,
            sizeof(response_buffer));
    }
    #if defined(DEBUG)
    else if (strcmp(command_buffer, "FAULT STACK") == 0)
    {
        (void)UartTx_QueueString(
            "[FAULT TEST] ENTER STACK OVERFLOW HOOK\r\n");
        osDelay(200U);
        SystemHealth_OnStackOverflow();
    }
    else if (strcmp(command_buffer, "FAULT MALLOC") == 0)
    {
        (void)UartTx_QueueString(
            "[FAULT TEST] ENTER MALLOC FAILED HOOK\r\n");
        osDelay(200U);
        SystemHealth_OnMallocFailed();
    }
    else if (strcmp(command_buffer, "FAULT HARD") == 0)
    {
        (void)UartTx_QueueString(
            "[FAULT TEST] TRIGGER HARDFAULT\r\n");
        osDelay(200U);
        __asm volatile ("udf #0");
    }
    else if (strcmp(command_buffer, "FAULT WATCHDOG") == 0)
    {
        SystemHealth_TriggerWatchdogFault();
    }
    #endif
    else if (strcmp(command_buffer, "LED ON") == 0)
    {
        control_value = CONTROL_LED_ON;

        if (osMessageQueuePut(
                protocol_context.control_queue,
                &control_value,
                0U,
                20U) != osOK)
        {
            (void)UartTx_QueueString(
                "ERROR: CONTROL QUEUE FULL\r\n");
        }
    }
    else if (strcmp(command_buffer, "LED OFF") == 0)
    {
        control_value = CONTROL_LED_OFF;

        if (osMessageQueuePut(
                protocol_context.control_queue,
                &control_value,
                0U,
                20U) != osOK)
        {
            (void)UartTx_QueueString(
                "ERROR: CONTROL QUEUE FULL\r\n");
        }
    }
    else
    {
        (void)snprintf(
            response_buffer,
            sizeof(response_buffer),
            "UNKNOWN COMMAND: %s\r\n"
            "TYPE HELP\r\n",
            command_buffer);

        (void)UartTx_QueueString(response_buffer);
    }
}


void CommandTask_Run(void *argument)
{
    uint8_t received_byte = 0U;
    uint8_t rx_chunk[16] = {0};
    size_t received_length = 0U;
    size_t rx_index = 0U;
    uint8_t discard_until_enter = 0U;
    uint8_t previous_was_cr = 0U;
    uint32_t command_index = 0U;
    char command_buffer[32] = {0};
    char response_buffer[128] = {0};

    (void)argument;

    osDelay(100U);

    if ((protocol_context.uart == NULL) ||
        (HAL_UART_Receive_IT(
            protocol_context.uart,
            &uart_rx_byte,
            1U) != HAL_OK))
    {
        (void)UartTx_QueueString(
            "[CMD] UART RX START ERROR\r\n");
    }

    SystemHealth_PrintResetCause();

    osDelay(300U);
    SelfTest_Run();

    for (;;)
    {
        received_length =
            xStreamBufferReceive(
                protocol_context.rx_stream_buffer,
                rx_chunk,
                sizeof(rx_chunk),
                pdMS_TO_TICKS(20U));

        if (received_length == 0U)
        {
            (void)UartProtocol_CheckTimeout(
                HAL_GetTick());
            continue;
        }

        for (rx_index = 0U;
             rx_index < received_length;
             rx_index++)
        {
            received_byte = rx_chunk[rx_index];

            if (UartProtocol_ProcessByte(
                    received_byte,
                    HAL_GetTick()) != 0U)
            {
                command_index = 0U;
                command_buffer[0] = '\0';
                discard_until_enter = 0U;
                previous_was_cr = 0U;
                continue;
            }

            if ((received_byte == '\r') ||
                (received_byte == '\n'))
            {
                if ((received_byte == '\n') &&
                    (previous_was_cr != 0U))
                {
                    previous_was_cr = 0U;
                    continue;
                }

                previous_was_cr =
                    (received_byte == '\r') ? 1U : 0U;

                if (discard_until_enter != 0U)
                {
                    discard_until_enter = 0U;
                    command_index = 0U;
                    command_buffer[0] = '\0';

                    (void)UartTx_QueueString(
                        "\r\n[CMD] ERROR: COMMAND TOO LONG\r\n");
                    continue;
                }

                if (command_index == 0U)
                {
                    continue;
                }

                command_buffer[command_index] = '\0';

                (void)snprintf(
                    response_buffer,
                    sizeof(response_buffer),
                    "\r\n[CMD] %s\r\n",
                    command_buffer);

                (void)UartTx_QueueString(response_buffer);
                CommandTask_ProcessCommand(command_buffer);

                command_index = 0U;
                command_buffer[0] = '\0';
                continue;
            }

            previous_was_cr = 0U;

            if (discard_until_enter != 0U)
            {
                continue;
            }

            if (command_index < (sizeof(command_buffer) - 1U))
            {
                if ((received_byte >= (uint8_t)'a') &&
                    (received_byte <= (uint8_t)'z'))
                {
                    received_byte =
                        (uint8_t)(
                            received_byte -
                            ((uint8_t)'a' - (uint8_t)'A'));
                }

                command_buffer[command_index] =
                    (char)received_byte;
                command_index++;
            }
            else
            {
                discard_until_enter = 1U;
                command_index = 0U;
                command_buffer[0] = '\0';
            }
        }
    }
}


static void UartProtocol_SendError(
    const UartProtocolPacket_t *request,
    uint8_t error_code)
{
    UartProtocolPacket_t response_packet = {0};

    if (request == NULL)
    {
        return;
    }

    response_packet.version = UART_PROTOCOL_VERSION;
    response_packet.type = UART_PACKET_TYPE_ERROR;
    response_packet.sequence = request->sequence;
    response_packet.length = 2U;
    response_packet.payload[0] = request->type;
    response_packet.payload[1] = error_code;

    if (UartProtocol_SendResponseAndCache(
            request,
            &response_packet) != HAL_OK)
    {
        (void)UartTx_QueueString(
            "[PKT] ERROR RESPONSE TX FAILED\r\n");
    }
}


static void UartProtocol_OnPacket(
    const UartProtocolPacket_t *packet)
{
    char log_buffer[128];
    HAL_StatusTypeDef binary_tx_status = HAL_ERROR;
    uint8_t control_value = 0U;
    UartProtocolPacket_t response_packet = {0};
    uint8_t response_frame[UART_PROTOCOL_MAX_FRAME_SIZE] = {0};
    uint16_t response_length = 0U;
    SensorMessage_t sensor_snapshot = {0};

    if (packet == NULL)
    {
        return;
    }

    valid_packet_count++;

    if (UartProtocol_IsDuplicateRequest(packet) != 0U)
    {
        duplicate_request_count++;
        binary_tx_status = UartProtocol_ResendCachedResponse();

        if (binary_tx_status == HAL_OK)
        {
            (void)snprintf(
                log_buffer,
                sizeof(log_buffer),
                "[PKT] DUPLICATE | "
                "TYPE 0x%02X | SEQ 0x%02X | "
                "REPLAY | VALID %lu | DUP %lu\r\n",
                (unsigned int)packet->type,
                (unsigned int)packet->sequence,
                (unsigned long)valid_packet_count,
                (unsigned long)duplicate_request_count);
        }
        else
        {
            (void)snprintf(
                log_buffer,
                sizeof(log_buffer),
                "[PKT] DUPLICATE | "
                "TYPE 0x%02X | SEQ 0x%02X | "
                "REPLAY FAILED | VALID %lu | DUP %lu\r\n",
                (unsigned int)packet->type,
                (unsigned int)packet->sequence,
                (unsigned long)valid_packet_count,
                (unsigned long)duplicate_request_count);
        }

        (void)UartTx_QueueString(log_buffer);
        return;
    }

    switch (packet->type)
    {
        case UART_PACKET_TYPE_PING:
            if (packet->length != 0U)
            {
                UartProtocol_SendError(
                    packet,
                    UART_PROTOCOL_ERROR_INVALID_LENGTH);

                (void)snprintf(
                    log_buffer,
                    sizeof(log_buffer),
                    "[PKT] PING ERROR | "
                    "INVALID LENGTH %u\r\n",
                    (unsigned int)packet->length);

                (void)UartTx_QueueString(log_buffer);
                break;
            }

            (void)snprintf(
                log_buffer,
                sizeof(log_buffer),
                "[PKT] PING | SEQ 0x%02X | OK\r\n",
                (unsigned int)packet->sequence);

            (void)UartTx_QueueString(log_buffer);

            response_packet.version = UART_PROTOCOL_VERSION;
            response_packet.type = UART_PACKET_TYPE_PONG;
            response_packet.sequence = packet->sequence;
            response_packet.length = 0U;

            response_length =
                UartProtocol_EncodePacket(
                    &response_packet,
                    response_frame,
                    sizeof(response_frame));

            if (response_length > 0U)
            {
                UartProtocol_StoreTransaction(
                    packet,
                    response_frame,
                    response_length);

                binary_tx_status =
                    UartTx_QueueData(
                        response_frame,
                        response_length);

                if (binary_tx_status != HAL_OK)
                {
                    (void)snprintf(
                        log_buffer,
                        sizeof(log_buffer),
                        "[PKT] BINARY TX ERROR: %u\r\n",
                        (unsigned int)binary_tx_status);

                    (void)UartTx_QueueString(log_buffer);
                }

                UartProtocol_PrintFrameHex(
                    response_frame,
                    response_length);
            }
            else
            {
                (void)UartTx_QueueString(
                    "[PKT] ENCODE ERROR\r\n");
            }
            break;

        case UART_PACKET_TYPE_GET_STATUS:
        {
            uint16_t distance_tenth_cm;

            if (packet->length != 0U)
            {
                UartProtocol_SendError(
                    packet,
                    UART_PROTOCOL_ERROR_INVALID_LENGTH);

                (void)snprintf(
                    log_buffer,
                    sizeof(log_buffer),
                    "[PKT] STATUS ERROR | "
                    "INVALID LENGTH %u\r\n",
                    (unsigned int)packet->length);

                (void)UartTx_QueueString(log_buffer);
                break;
            }

            if (App_GetSensorSnapshot(&sensor_snapshot) == 0U)
            {
                UartProtocol_SendError(
                    packet,
                    UART_PROTOCOL_ERROR_SNAPSHOT_FAILED);

                (void)UartTx_QueueString(
                    "[PKT] STATUS ERROR | "
                    "SNAPSHOT FAILED\r\n");
                break;
            }

            if (sensor_snapshot.distance_tenth_cm > UINT16_MAX)
            {
                distance_tenth_cm = UINT16_MAX;
            }
            else
            {
                distance_tenth_cm =
                    (uint16_t)sensor_snapshot.distance_tenth_cm;
            }

            response_packet.version = UART_PROTOCOL_VERSION;
            response_packet.type = UART_PACKET_TYPE_STATUS;
            response_packet.sequence = packet->sequence;
            response_packet.length = 3U;
            response_packet.payload[0] =
                (uint8_t)(distance_tenth_cm & 0x00FFU);
            response_packet.payload[1] =
                (uint8_t)((distance_tenth_cm >> 8U) & 0x00FFU);
            response_packet.payload[2] = sensor_snapshot.valid;

            if (UartProtocol_SendResponseAndCache(
                    packet,
                    &response_packet) != HAL_OK)
            {
                (void)UartTx_QueueString(
                    "[PKT] STATUS TX ERROR\r\n");
                break;
            }

            (void)snprintf(
                log_buffer,
                sizeof(log_buffer),
                "[PKT] STATUS | SEQ 0x%02X | "
                "DIST %lu.%lu cm | VALID %u\r\n",
                (unsigned int)packet->sequence,
                (unsigned long)(sensor_snapshot.distance_tenth_cm / 10U),
                (unsigned long)(sensor_snapshot.distance_tenth_cm % 10U),
                (unsigned int)sensor_snapshot.valid);

            (void)UartTx_QueueString(log_buffer);
            break;
        }

        case UART_PACKET_TYPE_LED_SET:
            if (packet->length != 1U)
            {
                UartProtocol_SendError(
                    packet,
                    UART_PROTOCOL_ERROR_INVALID_LENGTH);

                (void)snprintf(
                    log_buffer,
                    sizeof(log_buffer),
                    "[PKT] LED_SET ERROR | "
                    "INVALID LENGTH %u\r\n",
                    (unsigned int)packet->length);

                (void)UartTx_QueueString(log_buffer);
                break;
            }

            if (packet->payload[0] > 1U)
            {
                UartProtocol_SendError(
                    packet,
                    UART_PROTOCOL_ERROR_INVALID_PAYLOAD);

                (void)snprintf(
                    log_buffer,
                    sizeof(log_buffer),
                    "[PKT] LED_SET ERROR | "
                    "INVALID VALUE %u\r\n",
                    (unsigned int)packet->payload[0]);

                (void)UartTx_QueueString(log_buffer);
                break;
            }

            control_value =
                (packet->payload[0] == 1U)
                    ? CONTROL_LED_ON
                    : CONTROL_LED_OFF;

            if (osMessageQueuePut(
                    protocol_context.control_queue,
                    &control_value,
                    0U,
                    20U) != osOK)
            {
                UartProtocol_SendError(
                    packet,
                    UART_PROTOCOL_ERROR_CONTROL_QUEUE_FULL);

                (void)UartTx_QueueString(
                    "[PKT] LED_SET ERROR | "
                    "CONTROL QUEUE FULL\r\n");
                break;
            }

            UartProtocol_SendAck(packet);

            (void)snprintf(
                log_buffer,
                sizeof(log_buffer),
                "[PKT] LED_SET | SEQ 0x%02X | "
                "VALUE %u | ACCEPTED\r\n",
                (unsigned int)packet->sequence,
                (unsigned int)packet->payload[0]);

            (void)UartTx_QueueString(log_buffer);
            break;

        default:
            UartProtocol_SendError(
                packet,
                UART_PROTOCOL_ERROR_UNKNOWN_TYPE);

            (void)snprintf(
                log_buffer,
                sizeof(log_buffer),
                "[PKT] UNKNOWN TYPE 0x%02X | "
                "SEQ 0x%02X\r\n",
                (unsigned int)packet->type,
                (unsigned int)packet->sequence);

            (void)UartTx_QueueString(log_buffer);
            break;
    }
}
