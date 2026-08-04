# UART Binary Protocol 상세 설계

STM32와 PC 사이에서 Text 명령과 함께 사용할 수 있도록 길이 기반 Binary Protocol을 구현했습니다.

문자열 종료 문자에 의존하지 않고 `Length` 필드를 기준으로 Payload를 처리하며, CRC-16, Parser Timeout, Retry, 중복 요청 방지를 적용했습니다.

> GitHub 렌더링 오류를 피하기 위해 이 문서에서는 Markdown 표와 Mermaid를 사용하지 않습니다.

## 1. Packet 구조

```text
AA 55 | VERSION | TYPE | SEQUENCE | LENGTH | PAYLOAD | CRC_LOW CRC_HIGH
```

각 필드의 의미는 다음과 같습니다.

- `SOF1`
  - 크기: 1Byte
  - 값: `0xAA`
  - 역할: Packet 시작 위치 탐색

- `SOF2`
  - 크기: 1Byte
  - 값: `0x55`
  - 역할: 두 번째 시작 Byte 확인

- `Version`
  - 크기: 1Byte
  - 현재 값: `0x01`
  - 역할: Protocol Version 구분

- `Type`
  - 크기: 1Byte
  - 역할: 요청 또는 응답 종류 구분

- `Sequence`
  - 크기: 1Byte
  - 역할: 요청과 응답 연결 및 Retry 요청 식별

- `Length`
  - 크기: 1Byte
  - 역할: Payload 길이 표시
  - 최대 크기: 32Byte

- `Payload`
  - 크기: 0~32Byte
  - 역할: 명령 또는 응답 데이터

- `CRC Low`
  - 크기: 1Byte
  - 역할: CRC 하위 Byte

- `CRC High`
  - 크기: 1Byte
  - 역할: CRC 상위 Byte

## 2. Length 기반 Packet 처리

Binary Payload에는 문자열 종료 문자인 `\r`, `\n`, `\0`과 같은 값이 정상 데이터로 포함될 수 있습니다.

따라서 종료 문자를 기준으로 Packet 끝을 판단하지 않고, `Length` 필드에 기록된 크기만큼 Payload를 수신한 뒤 CRC를 확인하도록 구성했습니다.

```text
SOF 탐색
→ Version 수신
→ Type 수신
→ Sequence 수신
→ Length 확인
→ Length만큼 Payload 수신
→ CRC Low 수신
→ CRC High 수신
→ CRC 검증
→ Packet Handler 실행
```

Payload 최대 길이는 32Byte입니다.

`Length`가 허용 범위를 초과하면 Packet을 실행하지 않고 Parser를 초기 상태로 되돌립니다.

## 3. CRC 설정

- 알고리즘: `CRC-16/CCITT-FALSE`
- Polynomial: `0x1021`
- Initial Value: `0xFFFF`
- Final XOR: `0x0000`
- Input Reflection: 사용하지 않음
- Output Reflection: 사용하지 않음
- 전송 순서: Low Byte 먼저 전송

CRC 계산 범위는 SOF를 제외한 다음 영역입니다.

```text
VERSION + TYPE + SEQUENCE + LENGTH + PAYLOAD
```

SOF는 Packet 시작 위치를 찾는 경계값으로 사용하고, 실제 Packet 내용의 무결성은 Version부터 Payload까지 검증하도록 분리했습니다.

### CRC 검증 예시

입력 데이터:

```text
01 01 10 00
```

CRC 계산 결과:

```text
0xC637
```

Low Byte를 먼저 전송하므로 실제 CRC 전송 순서는 다음과 같습니다.

```text
37 C6
```

## 4. 요청 Packet Type

### PING

- 값: `0x01`
- 기능: PC와 MCU 사이 통신 연결 확인
- 정상 응답: `PONG`

### GET_STATUS

- 값: `0x02`
- 기능: 거리값과 센서 유효 상태 조회
- 정상 응답: `STATUS`

### LED_SET

- 값: `0x03`
- 기능: NUCLEO LD2 ON/OFF 제어
- 정상 응답: `ACK`

## 5. 응답 Packet Type

### PONG

- 값: `0x81`
- 기능: `PING` 요청의 정상 응답

### STATUS

- 값: `0x82`
- 기능: 거리값과 센서 유효 상태 전달

### ACK

- 값: `0x83`
- 기능: 제어 명령 접수 성공 전달

### ERROR

- 값: `0xFF`
- 기능: 잘못된 요청 또는 처리 실패 전달

## 6. Error Code

### OK

- 값: `0x00`
- 발생 조건: 요청이 정상적으로 처리됨

### INVALID_LENGTH

- 값: `0x01`
- 발생 조건: 요청 Payload 길이가 명령 형식과 일치하지 않음

### INVALID_PAYLOAD

- 값: `0x02`
- 발생 조건: 허용되지 않은 Payload 값이 전달됨

### UNKNOWN_TYPE

- 값: `0x03`
- 발생 조건: 정의되지 않은 Packet Type이 전달됨

### SNAPSHOT_FAILED

- 값: `0x04`
- 발생 조건: 센서 Snapshot을 읽지 못함

### CONTROL_QUEUE_FULL

- 값: `0x05`
- 발생 조건: 제어 요청을 `controlQueue`에 등록하지 못함

## 7. Binary Packet 처리 흐름

```text
USART2 RX ISR
→ 수신 Byte를 Stream Buffer에 저장
→ 다음 UART 수신 등록
→ commandTask가 Stream Buffer에서 Byte 수신
→ Binary Parser State Machine에서 Packet 조립
```

CRC 오류가 발생한 경우:

```text
CRC 불일치
→ Packet Handler 실행하지 않음
→ 명령 실행하지 않음
→ 응답 전송하지 않음
→ CRC ERROR 증가
→ Parser 초기화
```

Packet이 100ms 동안 완성되지 않은 경우:

```text
부분 Packet 수신
→ 100ms 동안 Packet 미완성
→ TIMEOUT 증가
→ 부분 Packet 폐기
→ Parser 초기화
→ 다음 정상 Packet 수신 대기
```

CRC가 정상인 경우:

```text
CRC 정상
→ 최근 요청 Cache와 비교
```

동일 요청인 경우:

```text
Version, Type, Sequence, Length, Payload 모두 동일
→ 명령 재실행 생략
→ DUPLICATE 증가
→ Cached Response 재전송
```

새로운 요청인 경우:

```text
요청 길이와 Payload 검증
→ PING, 상태 조회 또는 LED 제어 실행
→ 응답 Frame 생성
→ 요청과 응답 Cache 저장
→ uartTxQueue 등록
→ uartTxTask에서 UART DMA 송신
```

## 8. CRC 오류 처리

수신 Packet의 CRC를 다시 계산한 뒤 수신된 CRC와 비교합니다.

CRC가 일치하지 않으면 손상된 Packet이 실제 장치 제어로 이어지지 않도록 실행 전에 차단합니다.

```text
CRC 정상
→ Packet Handler 실행

CRC 오류
→ Packet 폐기
→ 오류 통계 증가
→ Parser 초기화
```

## 9. 불완전 Packet Timeout

SOF를 수신한 뒤 Packet이 완성되지 않은 상태가 100ms 이상 지속되면 부분 Packet을 폐기합니다.

`commandTask`는 Stream Buffer를 최대 20ms 동안 대기합니다. 수신 데이터가 없더라도 `UartProtocol_CheckTimeout()`을 호출해 Parser Timeout을 확인합니다.

```text
SOF 수신
→ Packet 일부만 수신
→ 100ms 동안 완성되지 않음
→ 부분 Packet 폐기
→ Parser 초기화
→ 다음 정상 Packet 처리
```

## 10. 응답 유실 시 Retry

Python 테스트 프로그램은 응답이 0.5초 안에 도착하지 않으면 동일 요청을 최대 3회까지 재전송합니다.

Retry에서는 새로운 Sequence를 만들지 않고 기존 Sequence를 유지합니다.

```text
첫 번째 요청 : TYPE 0x01 / SEQ 0x50
재전송 요청  : TYPE 0x01 / SEQ 0x50
```

같은 Sequence를 유지해야 MCU가 동일한 논리적 요청의 재전송인지 판단할 수 있습니다.

## 11. 중복 요청 실행 방지

첫 요청이 MCU에서 처리됐지만 응답만 유실된 경우 PC는 동일 요청을 다시 전송합니다.

Retry Packet을 새로운 명령으로 다시 실행하면 LED 제어 같은 명령이 중복 수행될 수 있습니다.

이를 방지하기 위해 가장 최근에 처리한 요청과 응답 Frame을 Cache에 저장했습니다.

중복 요청 판단 조건:

- Version 동일
- Type 동일
- Sequence 동일
- Length 동일
- Payload 동일

처리 흐름:

```text
신규 요청
→ 명령 실행
→ 응답 Frame 생성
→ 요청과 응답 Cache 저장
→ 응답 전송

동일 요청 Retry
→ Cache와 요청 비교
→ 명령 재실행 생략
→ Cached Response 재전송
```

Sequence가 같더라도 Type, Length 또는 Payload가 다르면 새로운 요청으로 처리합니다.

## 12. UART 송신 경로

Binary 응답은 여러 Task가 UART HAL 함수를 직접 호출하지 않고 `uartTxQueue`를 통해 `uartTxTask`에 전달합니다.

```text
Packet Handler
→ 응답 Frame 생성
→ uartTxQueue 등록
→ uartTxTask 수신
→ HAL_UART_Transmit_DMA 실행
→ DMA 완료 Callback
→ Direct Task Notification
→ 다음 메시지 처리
```

Queue 등록에 실패하면 `TX FAIL` 카운터를 증가시킵니다.

## 13. 통신 진단 통계

PuTTY에서 다음 Text 명령으로 통계를 확인할 수 있습니다.

```text
PKT STAT
```

통계 항목:

- `VALID`: CRC 검증을 통과해 Handler까지 전달된 Packet 수
- `DUPLICATE`: 동일 요청으로 판단돼 명령을 재실행하지 않은 Packet 수
- `CRC ERROR`: CRC가 일치하지 않은 Packet 수
- `TIMEOUT`: 완성되지 못하고 Parser Timeout 처리된 Packet 수
- `RX DROP`: Stream Buffer에 저장하지 못한 UART 수신 Byte 수
- `TX FAIL`: UART TX Queue 등록 실패 횟수

10회 반복 테스트 후 결과:

```text
VALID 140 | DUPLICATE 20
CRC ERROR 10 | TIMEOUT 10 | RX DROP 0 | TX FAIL 0
```

## 14. 자동 테스트 결과

Python 자동 테스트 항목:

1. PING
2. 상태 조회
3. LED ON
4. LED OFF
5. 잘못된 LED Payload
6. 정의되지 않은 Type
7. 응답 유실 및 Retry
8. 동일 요청 재전송
9. 동일 Sequence와 다른 Payload
10. 잘못된 CRC 이후 복구
11. 불완전 Packet Timeout 이후 복구

반복 결과:

```text
11개 테스트 × 10회 반복
총 결과: 110/110 PASS
```

## 15. 현재 한계

현재 중복 요청 Cache는 가장 최근 Transaction 한 개만 저장합니다.

여러 요청이 연속으로 처리된 뒤 과거 요청이 다시 도착하면 Cache에서 제거돼 새로운 요청으로 처리될 수 있습니다.

향후에는 여러 Transaction을 일정 시간 동안 보관하는 다중 Cache 구조로 확장할 수 있습니다.
