import time
from typing import Callable, Optional

import serial


# ============================================================
# Serial 설정
# ============================================================

PORT = "COM7"
BAUD_RATE = 115200

SERIAL_READ_TIMEOUT_SECONDS = 0.05
RESPONSE_TIMEOUT_SECONDS = 0.5
MAX_REQUEST_ATTEMPTS = 3


# ============================================================
# Protocol 기본 설정
# ============================================================

SOF1 = 0xAA
SOF2 = 0x55
VERSION = 0x01

HEADER_SIZE = 6
CRC_SIZE = 2
MAX_PAYLOAD_SIZE = 32


# ============================================================
# Request Type
# ============================================================

TYPE_PING = 0x01
TYPE_GET_STATUS = 0x02
TYPE_LED_SET = 0x03


# ============================================================
# Response Type
# ============================================================

TYPE_PONG = 0x81
TYPE_STATUS = 0x82
TYPE_ACK = 0x83
TYPE_ERROR = 0xFF


# ============================================================
# Result / Error Code
# ============================================================

RESULT_OK = 0x00

ERROR_INVALID_LENGTH = 0x01
ERROR_INVALID_PAYLOAD = 0x02
ERROR_UNKNOWN_TYPE = 0x03
ERROR_SNAPSHOT_FAILED = 0x04
ERROR_CONTROL_QUEUE_FULL = 0x05


# ============================================================
# 출력 Helper
# ============================================================

def format_hex(data: bytes) -> str:
    """Byte 데이터를 사람이 읽을 수 있는 HEX 문자열로 변환한다."""
    return " ".join(f"{byte:02X}" for byte in data)


# ============================================================
# CRC-16/CCITT-FALSE
# ============================================================

def calculate_crc(data: bytes) -> int:
    """
    CRC-16/CCITT-FALSE

    Polynomial : 0x1021
    Initial    : 0xFFFF
    Final XOR  : 0x0000
    Reflection : 없음
    """
    crc = 0xFFFF

    for byte in data:
        crc ^= byte << 8

        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF

    return crc


# ============================================================
# Packet 생성
# ============================================================

def make_packet(
    packet_type: int,
    sequence: int,
    payload: bytes = b"",
) -> bytes:
    """
    Packet 형식:

    SOF1
    SOF2
    VERSION
    TYPE
    SEQUENCE
    LENGTH
    PAYLOAD
    CRC_LOW
    CRC_HIGH

    CRC 계산 대상:

    VERSION + TYPE + SEQUENCE + LENGTH + PAYLOAD
    """
    if not 0 <= packet_type <= 0xFF:
        raise ValueError("Packet type must be between 0 and 255.")

    if not 0 <= sequence <= 0xFF:
        raise ValueError("Sequence must be between 0 and 255.")

    if len(payload) > MAX_PAYLOAD_SIZE:
        raise ValueError(
            f"Payload must not exceed "
            f"{MAX_PAYLOAD_SIZE} bytes."
        )

    crc_data = bytes([
        VERSION,
        packet_type,
        sequence,
        len(payload),
    ]) + payload

    calculated_crc = calculate_crc(crc_data)

    crc_low = calculated_crc & 0xFF
    crc_high = (calculated_crc >> 8) & 0xFF

    return (
        bytes([
            SOF1,
            SOF2,
        ])
        + crc_data
        + bytes([
            crc_low,
            crc_high,
        ])
    )


# ============================================================
# 수신 Packet 검사
# ============================================================

def packet_crc_is_valid(packet: bytes) -> bool:
    """수신 Packet의 길이와 CRC를 검증한다."""
    minimum_frame_size = HEADER_SIZE + CRC_SIZE

    if len(packet) < minimum_frame_size:
        return False

    if packet[0] != SOF1 or packet[1] != SOF2:
        return False

    if packet[2] != VERSION:
        return False

    payload_length = packet[5]

    if payload_length > MAX_PAYLOAD_SIZE:
        return False

    expected_frame_length = (
        HEADER_SIZE
        + payload_length
        + CRC_SIZE
    )

    if len(packet) != expected_frame_length:
        return False

    calculated_crc = calculate_crc(
        packet[2:-CRC_SIZE]
    )

    received_crc = (
        packet[-2]
        | (packet[-1] << 8)
    )

    return calculated_crc == received_crc


# ============================================================
# Binary Packet 수신
# ============================================================

def read_packet(
    ser: serial.Serial,
    expected_sequence: int,
    timeout_seconds: float,
) -> Optional[bytes]:
    """
    ASCII 로그가 섞인 UART Stream에서
    AA 55로 시작하는 Binary Packet을 찾는다.

    CRC가 정상이고 Sequence가 일치하는 Packet만 반환한다.
    """
    received_buffer = bytearray()

    deadline = (
        time.monotonic()
        + timeout_seconds
    )

    while time.monotonic() < deadline:
        read_size = ser.in_waiting or 1
        chunk = ser.read(read_size)

        if chunk:
            received_buffer.extend(chunk)

        while True:
            sof_index = received_buffer.find(
                bytes([SOF1, SOF2])
            )

            if sof_index < 0:
                # 마지막 Byte가 AA라면 다음 55와
                # SOF를 만들 가능성이 있으므로 보존한다.
                if (
                    received_buffer
                    and received_buffer[-1] == SOF1
                ):
                    received_buffer[:] = bytes([SOF1])
                else:
                    received_buffer.clear()

                break

            # SOF 앞의 ASCII 로그와 잡음은 폐기한다.
            if sof_index > 0:
                del received_buffer[:sof_index]

            if len(received_buffer) < HEADER_SIZE:
                break

            payload_length = received_buffer[5]

            if payload_length > MAX_PAYLOAD_SIZE:
                # 잘못된 SOF 후보일 수 있으므로
                # 첫 Byte만 버리고 다시 탐색한다.
                del received_buffer[0]
                continue

            frame_length = (
                HEADER_SIZE
                + payload_length
                + CRC_SIZE
            )

            if len(received_buffer) < frame_length:
                break

            packet = bytes(
                received_buffer[:frame_length]
            )

            del received_buffer[:frame_length]

            if not packet_crc_is_valid(packet):
                print(
                    "WARN: Invalid response CRC: "
                    f"{format_hex(packet)}"
                )
                continue

            packet_sequence = packet[4]

            if packet_sequence != expected_sequence:
                print(
                    "WARN: Unexpected sequence: "
                    f"0x{packet_sequence:02X}"
                )
                continue

            return packet

    return None


# ============================================================
# 요청 송신 + Timeout + Retry
# ============================================================

def send_request(
    ser: serial.Serial,
    name: str,
    request: bytes,
    max_attempts: int = MAX_REQUEST_ATTEMPTS,
    response_timeout: float = RESPONSE_TIMEOUT_SECONDS,
    simulate_first_response_loss: bool = False,
) -> Optional[bytes]:
    """
    요청을 전송하고 응답을 기다린다.

    응답 Timeout이 발생하면 같은 Packet과 같은 Sequence로
    최대 max_attempts만큼 재전송한다.

    simulate_first_response_loss=True인 경우 첫 번째 응답을
    PC 측에서 의도적으로 폐기해 Retry를 검증한다.
    """
    if len(request) < HEADER_SIZE + CRC_SIZE:
        raise ValueError("Request packet is too short.")

    sequence = request[4]

    ser.reset_input_buffer()

    print(f"\n=== {name} ===")
    print(f"TX: {format_hex(request)}")

    for attempt in range(1, max_attempts + 1):
        print(
            f"Attempt {attempt}/{max_attempts} "
            f"| SEQ 0x{sequence:02X}"
        )

        bytes_written = ser.write(request)
        ser.flush()

        if bytes_written != len(request):
            print(
                f"WARN: Only {bytes_written}/"
                f"{len(request)} bytes were written."
            )

        if (
            simulate_first_response_loss
            and attempt == 1
        ):
            # STM32가 응답할 시간을 준다.
            time.sleep(response_timeout)

            # 실제로 도착한 첫 번째 응답과 로그를
            # PC 수신 Buffer에서 의도적으로 폐기한다.
            ser.reset_input_buffer()

            print(
                "SIMULATION: "
                "First response discarded."
            )
            print(
                "TIMEOUT: "
                "Retrying the same request."
            )

            continue

        response = read_packet(
            ser=ser,
            expected_sequence=sequence,
            timeout_seconds=response_timeout,
        )

        if response is not None:
            print(f"RX: {format_hex(response)}")
            print("CRC: PASS")

            if attempt > 1:
                print(
                    "RETRY SUCCESS: "
                    f"Response received on "
                    f"attempt {attempt}."
                )

            return response

        print(
            "TIMEOUT: No valid response for "
            f"{response_timeout:.1f} seconds."
        )

        if attempt < max_attempts:
            print(
                "RETRY: Sending the same "
                "request and sequence."
            )

    print(
        "FAIL: No valid response after "
        f"{max_attempts} attempts."
    )

    return None


# ============================================================
# 응답 공통 검사
# ============================================================

def check_packet(
    response: Optional[bytes],
    expected_type: int,
    expected_sequence: int,
    expected_payload: bytes,
) -> bool:
    """응답 Type, Sequence, Length, Payload, CRC를 검증한다."""
    if response is None:
        return False

    if len(response) < HEADER_SIZE + CRC_SIZE:
        print("FAIL: Response frame is too short.")
        return False

    actual_type = response[3]
    actual_sequence = response[4]
    actual_length = response[5]
    actual_payload = response[6:-CRC_SIZE]

    success = (
        actual_type == expected_type
        and actual_sequence == expected_sequence
        and actual_length == len(expected_payload)
        and actual_payload == expected_payload
        and packet_crc_is_valid(response)
    )

    if success:
        print("PASS")
        return True

    print("FAIL: Response does not match.")

    expected_packet = make_packet(
        packet_type=expected_type,
        sequence=expected_sequence,
        payload=expected_payload,
    )

    print(
        f"EXPECTED: "
        f"{format_hex(expected_packet)}"
    )

    print(
        f"ACTUAL:   "
        f"{format_hex(response)}"
    )

    return False


# ============================================================
# Test 1: PING
# ============================================================

def test_ping(
    ser: serial.Serial,
) -> bool:
    sequence = 0x10

    request = make_packet(
        packet_type=TYPE_PING,
        sequence=sequence,
    )

    response = send_request(
        ser=ser,
        name="PING",
        request=request,
    )

    return check_packet(
        response=response,
        expected_type=TYPE_PONG,
        expected_sequence=sequence,
        expected_payload=b"",
    )


# ============================================================
# Test 2: GET_STATUS
# ============================================================

def test_get_status(
    ser: serial.Serial,
) -> bool:
    sequence = 0x20

    request = make_packet(
        packet_type=TYPE_GET_STATUS,
        sequence=sequence,
    )

    response = send_request(
        ser=ser,
        name="GET_STATUS",
        request=request,
    )

    if response is None:
        return False

    response_type = response[3]
    response_sequence = response[4]
    payload_length = response[5]
    payload = response[6:-CRC_SIZE]

    if (
        response_type != TYPE_STATUS
        or response_sequence != sequence
        or payload_length != 3
        or len(payload) != 3
        or not packet_crc_is_valid(response)
    ):
        print("FAIL: Invalid STATUS response format.")
        return False

    # Little Endian 거리값 복원
    distance_tenth_cm = (
        payload[0]
        | (payload[1] << 8)
    )

    sensor_valid = payload[2]

    print(
        "Distance: "
        f"{distance_tenth_cm / 10.0:.1f} cm"
    )

    print(
        f"Sensor valid: {sensor_valid}"
    )

    if sensor_valid not in (0, 1):
        print(
            "FAIL: Sensor valid field "
            "must be 0 or 1."
        )
        return False

    print("PASS")
    return True


# ============================================================
# Test 3: LED ON
# ============================================================

def test_led_on(
    ser: serial.Serial,
) -> bool:
    sequence = 0x30

    request = make_packet(
        packet_type=TYPE_LED_SET,
        sequence=sequence,
        payload=bytes([0x01]),
    )

    response = send_request(
        ser=ser,
        name="LED ON",
        request=request,
    )

    return check_packet(
        response=response,
        expected_type=TYPE_ACK,
        expected_sequence=sequence,
        expected_payload=bytes([
            TYPE_LED_SET,
            RESULT_OK,
        ]),
    )


# ============================================================
# Test 4: LED OFF
# ============================================================

def test_led_off(
    ser: serial.Serial,
) -> bool:
    sequence = 0x31

    request = make_packet(
        packet_type=TYPE_LED_SET,
        sequence=sequence,
        payload=bytes([0x00]),
    )

    response = send_request(
        ser=ser,
        name="LED OFF",
        request=request,
    )

    return check_packet(
        response=response,
        expected_type=TYPE_ACK,
        expected_sequence=sequence,
        expected_payload=bytes([
            TYPE_LED_SET,
            RESULT_OK,
        ]),
    )


# ============================================================
# Test 5: 잘못된 LED 값
# ============================================================

def test_invalid_led_value(
    ser: serial.Serial,
) -> bool:
    sequence = 0x32

    request = make_packet(
        packet_type=TYPE_LED_SET,
        sequence=sequence,
        payload=bytes([0x05]),
    )

    response = send_request(
        ser=ser,
        name="INVALID LED VALUE",
        request=request,
    )

    return check_packet(
        response=response,
        expected_type=TYPE_ERROR,
        expected_sequence=sequence,
        expected_payload=bytes([
            TYPE_LED_SET,
            ERROR_INVALID_PAYLOAD,
        ]),
    )


# ============================================================
# Test 6: 알 수 없는 TYPE
# ============================================================

def test_unknown_type(
    ser: serial.Serial,
) -> bool:
    sequence = 0x40
    unknown_type = 0x7E

    request = make_packet(
        packet_type=unknown_type,
        sequence=sequence,
    )

    response = send_request(
        ser=ser,
        name="UNKNOWN TYPE",
        request=request,
    )

    return check_packet(
        response=response,
        expected_type=TYPE_ERROR,
        expected_sequence=sequence,
        expected_payload=bytes([
            unknown_type,
            ERROR_UNKNOWN_TYPE,
        ]),
    )


# ============================================================
# Test 7: 첫 번째 응답 폐기 후 Retry
# ============================================================

def test_retry_after_response_loss(
    ser: serial.Serial,
) -> bool:
    sequence = 0x50

    request = make_packet(
        packet_type=TYPE_PING,
        sequence=sequence,
    )

    response = send_request(
        ser=ser,
        name="PING RETRY TEST",
        request=request,
        max_attempts=3,
        response_timeout=0.5,
        simulate_first_response_loss=True,
    )

    return check_packet(
        response=response,
        expected_type=TYPE_PONG,
        expected_sequence=sequence,
        expected_payload=b"",
    )
def test_duplicate_request_replay(
    ser: serial.Serial,
) -> bool:
    """
    같은 LED ON 요청을 두 번 보낸다.

    두 번째 요청은 STM32에서 명령을 다시 실행하지 않고
    저장된 ACK Frame만 재전송해야 한다.
    """
    sequence = 0x60

    request = make_packet(
        packet_type=TYPE_LED_SET,
        sequence=sequence,
        payload=bytes([0x01]),
    )

    first_response = send_request(
        ser=ser,
        name="DUPLICATE LED ON - FIRST",
        request=request,
        max_attempts=1,
        response_timeout=0.5,
    )

    if not check_packet(
        response=first_response,
        expected_type=TYPE_ACK,
        expected_sequence=sequence,
        expected_payload=bytes([
            TYPE_LED_SET,
            RESULT_OK,
        ]),
    ):
        return False

    time.sleep(0.1)

    second_response = send_request(
        ser=ser,
        name="DUPLICATE LED ON - SECOND",
        request=request,
        max_attempts=1,
        response_timeout=0.5,
    )

    if not check_packet(
        response=second_response,
        expected_type=TYPE_ACK,
        expected_sequence=sequence,
        expected_payload=bytes([
            TYPE_LED_SET,
            RESULT_OK,
        ]),
    ):
        return False

    if first_response != second_response:
        print("FAIL: Cached response is different.")
        return False

    print("DUPLICATE REPLAY: SAME RESPONSE FRAME")
    print("PASS")

    return True


def test_same_sequence_different_payload(
    ser: serial.Serial,
) -> bool:
    """
    Sequence는 같지만 Payload가 다른 두 요청을 보낸다.

    LED ON과 LED OFF는 서로 다른 요청이므로
    두 요청 모두 새 요청으로 실행되어야 한다.
    """
    sequence = 0x61

    led_on_request = make_packet(
        packet_type=TYPE_LED_SET,
        sequence=sequence,
        payload=bytes([0x01]),
    )

    led_off_request = make_packet(
        packet_type=TYPE_LED_SET,
        sequence=sequence,
        payload=bytes([0x00]),
    )

    led_on_response = send_request(
        ser=ser,
        name="SAME SEQ - LED ON",
        request=led_on_request,
        max_attempts=1,
        response_timeout=0.5,
    )

    if not check_packet(
        response=led_on_response,
        expected_type=TYPE_ACK,
        expected_sequence=sequence,
        expected_payload=bytes([
            TYPE_LED_SET,
            RESULT_OK,
        ]),
    ):
        return False

    time.sleep(0.1)

    led_off_response = send_request(
        ser=ser,
        name="SAME SEQ - LED OFF",
        request=led_off_request,
        max_attempts=1,
        response_timeout=0.5,
    )

    if not check_packet(
        response=led_off_response,
        expected_type=TYPE_ACK,
        expected_sequence=sequence,
        expected_payload=bytes([
            TYPE_LED_SET,
            RESULT_OK,
        ]),
    ):
        return False

    print(
        "SAME SEQUENCE + DIFFERENT PAYLOAD: "
        "BOTH ACCEPTED"
    )
    print("PASS")

    return True


def test_bad_crc_then_recovery(
    ser: serial.Serial,
) -> bool:
    """
    CRC가 손상된 PING을 보낸 뒤 정상 PING을 보낸다.

    손상 Packet:
    → 응답이 없어야 함

    정상 Packet:
    → PONG이 와야 함
    """
    sequence = 0x70

    valid_request = make_packet(
        packet_type=TYPE_PING,
        sequence=sequence,
    )

    corrupted_request = bytearray(valid_request)

    # CRC HIGH Byte의 한 Bit를 변조한다.
    corrupted_request[-1] ^= 0x01

    ser.reset_input_buffer()

    print("\n=== BAD CRC THEN RECOVERY ===")
    print(
        "BAD TX: "
        f"{format_hex(bytes(corrupted_request))}"
    )

    ser.write(bytes(corrupted_request))
    ser.flush()

    bad_response = read_packet(
        ser=ser,
        expected_sequence=sequence,
        timeout_seconds=0.3,
    )

    if bad_response is not None:
        print(
            "FAIL: STM32 responded to "
            "a corrupted packet."
        )
        return False

    print("BAD CRC: NO RESPONSE - PASS")

    time.sleep(0.1)

    valid_response = send_request(
        ser=ser,
        name="RECOVERY PING",
        request=valid_request,
        max_attempts=1,
        response_timeout=0.5,
    )

    if not check_packet(
        response=valid_response,
        expected_type=TYPE_PONG,
        expected_sequence=sequence,
        expected_payload=b"",
    ):
        return False

    print("CRC ERROR RECOVERY: PASS")
    return True


def test_partial_packet_timeout_recovery(
    ser: serial.Serial,
) -> bool:
    """
    Packet 앞부분만 보내고 150ms 동안 중단한다.

    STM32 Parser Timeout은 100ms이므로
    부분 Packet을 폐기한 뒤 다음 정상 PING을 처리해야 한다.
    """
    sequence = 0x71

    valid_request = make_packet(
        packet_type=TYPE_PING,
        sequence=sequence,
    )

    partial_packet = bytes([
        SOF1,
        SOF2,
        VERSION,
        TYPE_LED_SET,
    ])

    ser.reset_input_buffer()

    print("\n=== PARTIAL PACKET TIMEOUT RECOVERY ===")
    print(
        "PARTIAL TX: "
        f"{format_hex(partial_packet)}"
    )

    ser.write(partial_packet)
    ser.flush()

    # STM32 Parser Timeout 100ms보다 길게 기다린다.
    time.sleep(0.15)

    valid_response = send_request(
        ser=ser,
        name="PING AFTER PARTIAL PACKET",
        request=valid_request,
        max_attempts=1,
        response_timeout=0.5,
    )

    if not check_packet(
        response=valid_response,
        expected_type=TYPE_PONG,
        expected_sequence=sequence,
        expected_payload=b"",
    ):
        print(
            "FAIL: Parser did not recover "
            "after partial packet timeout."
        )
        return False

    print("PARTIAL PACKET RECOVERY: PASS")
    return True

# ============================================================
# Main
# ============================================================

def main() -> None:
    tests: list[
        Callable[[serial.Serial], bool]
    ] = [
        test_ping,
    test_get_status,
    test_led_on,
    test_led_off,
    test_invalid_led_value,
    test_unknown_type,
    test_retry_after_response_loss,
    test_duplicate_request_replay,
    test_same_sequence_different_payload,
    test_bad_crc_then_recovery,
    test_partial_packet_timeout_recovery,
    ]

    tests_passed = 0

    print(f"SCRIPT FILE: {__file__}")
    print("DAY45 CRC + TIMEOUT + RETRY VERSION")
    print(f"PORT: {PORT}")
    print(f"BAUD: {BAUD_RATE}")
    print("CRC: CRC-16/CCITT-FALSE")

    try:
        with serial.Serial(
            port=PORT,
            baudrate=BAUD_RATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=SERIAL_READ_TIMEOUT_SECONDS,
        ) as ser:
            # 보드 부팅 로그가 들어올 시간을 준다.
            time.sleep(1.0)

            ser.reset_input_buffer()
            ser.reset_output_buffer()

            for test in tests:
                try:
                    if test(ser):
                        tests_passed += 1

                except (
                    ValueError,
                    IndexError,
                ) as error:
                    print(
                        f"FAIL: Test exception: {error}"
                    )

                time.sleep(0.2)

    except serial.SerialException as error:
        print(f"\nSerial error: {error}")
        print(
            "PuTTY가 닫혀 있는지, "
            "COM 포트 번호가 맞는지 확인하세요."
        )
        return

    print("\n==========================")

    print(
        f"RESULT: {tests_passed}/"
        f"{len(tests)} tests passed"
    )

    if tests_passed == len(tests):
        print(
            "ALL CRC, TIMEOUT, "
            "AND RETRY TESTS PASSED"
        )
    else:
        print("SOME TESTS FAILED")


if __name__ == "__main__":
    main()