# 핵심 문제 해결 사례

프로젝트 진행 중 발생한 주요 문제를 문제 상황, 원인 분석, 수정 방법, 검증 결과 순서로 정리했습니다.

> GitHub 렌더링 오류를 피하기 위해 이 문서에서는 Markdown 표와 Mermaid를 사용하지 않습니다.

## 1. ASCII 문자열 전송에서 길이 기반 Binary Protocol로 개선

### 문제 상황

초기 UART 통신에서는 사람이 읽을 수 있는 문자열 중심으로 명령을 처리했습니다.

Binary Packet을 구현하는 과정에서 문자열 `"AA"`와 실제 Binary 값 `0xAA`가 서로 다른 데이터라는 점을 확인했습니다.

```text
문자열 "AA"
→ ASCII Byte: 0x41 0x41

실제 Binary 값 0xAA
→ Binary Byte: 0xAA
```

HEX 형태로 보이는 문자열을 전송해도 MCU에는 각 문자의 ASCII 코드가 수신됐습니다.

그 결과 Parser는 SOF 값인 `0xAA`를 찾지 못해 Packet을 정상적으로 조립하지 못했습니다.

### 원인 분석

문자열 `"AA"`는 문자 `A` 두 개입니다.

ASCII에서 문자 `A`는 `0x41`이므로 UART로 전송되는 실제 값은 다음과 같습니다.

```text
41 41
```

Binary SOF 값 `0xAA`는 하나의 Byte입니다.

```text
AA
```

또한 문자열 통신은 `\r`, `\n`, `\0` 같은 종료 문자를 사용할 수 있지만, Binary Payload에는 같은 값이 정상 데이터로 포함될 수 있습니다.

따라서 문자열 종료 방식으로는 Binary Packet 경계를 안정적으로 판단하기 어렵다고 분석했습니다.

### 수정 방법

고정된 SOF와 `Length` 필드를 사용하는 Binary Frame으로 변경했습니다.

```text
SOF | VERSION | TYPE | SEQUENCE | LENGTH | PAYLOAD | CRC
```

MCU Parser는 `Length` 값만큼 Payload를 수신한 뒤 CRC를 확인하도록 구성했습니다.

```text
SOF 탐색
→ Header 수신
→ Length 확인
→ Length만큼 Payload 수신
→ CRC 수신
→ CRC 검증
→ 명령 실행
```

Python 테스트 프로그램에서는 HEX 문자열을 보내지 않고 `bytes` 객체로 실제 Binary Byte 배열을 생성해 전송했습니다.

```python
packet = bytes([
    0xAA,
    0x55,
    0x01,
    0x01,
    0x10,
    0x00,
    0x37,
    0xC6,
])

serial_port.write(packet)
```

### 검증 방법

- PING Packet 전송 후 PONG 응답 확인
- 상태 조회 Packet 전송 후 거리값과 센서 상태 확인
- LED ON/OFF Binary 명령 실행
- CRC가 손상된 Packet 폐기 여부 확인
- Payload 값과 관계없이 Length 기준으로 Packet이 조립되는지 확인

### 검증 결과

- PING, 상태 조회, LED 제어 Binary Packet 정상 처리
- Payload 내부 값을 종료 문자로 잘못 판단하지 않음
- Length 기준 Packet 조립 정상 동작
- CRC-16으로 손상 Packet 실행 차단
- Python 자동 테스트 11개 통과

### 배운 점

로그에 같은 HEX 값이 보이더라도 실제 데이터가 문자열인지 Binary Byte인지 구분해야 한다는 점을 배웠습니다.

Binary Protocol에서는 종료 문자보다 명시적인 길이 정보와 상태 기반 Parser가 더 적합하다는 것을 확인했습니다.

---

## 2. Retry 요청에 의한 명령 중복 실행 방지

### 문제 상황

PC가 요청을 전송한 뒤 MCU 응답을 받지 못하면 같은 요청을 Retry하도록 구성했습니다.

첫 번째 요청이 MCU에서 이미 처리됐고 응답만 유실된 상황이라면, 재전송 요청을 다시 실행하면서 LED 제어 같은 명령이 중복 수행될 수 있었습니다.

```text
첫 번째 요청
→ MCU에서 명령 실행
→ 응답 전송
→ PC가 응답을 받지 못함

Retry 요청
→ 같은 명령을 다시 실행할 가능성
```

### 원인 분석

초기 구조에서는 같은 Sequence로 요청이 다시 들어와도 MCU가 새로운 요청과 구분하지 않고 Packet Handler를 재실행했습니다.

Sequence만 비교하는 방식도 안전하지 않았습니다.

Sequence는 1Byte이므로 반복 사용될 수 있고, 같은 Sequence를 가진 다른 Payload가 들어올 수 있기 때문입니다.

```text
SEQ 0x20 / LED ON
SEQ 0x20 / LED OFF
```

두 요청은 Sequence는 같지만 Payload가 다르므로 서로 다른 요청으로 처리해야 합니다.

### 수정 방법

가장 최근에 처리한 요청과 해당 응답 Frame을 Cache에 저장했습니다.

다음 항목이 모두 같을 때만 동일 요청의 Retry로 판단했습니다.

- Version
- Type
- Sequence
- Length
- Payload

중복 요청이면 명령 처리 코드를 다시 실행하지 않고 이전 응답 Frame만 `uartTxQueue`로 전송합니다.

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

응답을 TX Queue에 넣지 못하더라도 명령은 이미 실행됐을 수 있습니다.

따라서 응답 Frame Encode가 성공하면 실제 Queue 전송보다 먼저 요청과 응답을 Cache에 저장하도록 구성했습니다.

### 검증 방법

Python 자동 테스트에 다음 상황을 추가했습니다.

- 동일한 LED ON 요청을 연속 전송
- 첫 응답과 중복 요청 응답 Frame 비교
- 같은 Sequence에서 Payload만 바꿔 LED ON과 LED OFF 전송
- 첫 응답을 PC에서 의도적으로 무시한 뒤 Retry
- `DUPLICATE` 통계 증가 확인

### 검증 결과

- 동일 요청 Retry 시 명령 재실행 없이 Cached Response 재전송
- 같은 Sequence라도 Payload가 다르면 새로운 요청으로 처리
- 10회 반복 테스트에서 중복 요청 20개 감지
- 전체 자동 테스트 110/110 통과
- UART RX Drop 0회
- UART TX Queue Failure 0회

```text
VALID 140 | DUPLICATE 20
CRC ERROR 10 | TIMEOUT 10 | RX DROP 0 | TX FAIL 0
```

### 배운 점

Retry는 같은 Packet을 다시 보내는 기능만으로 끝나는 것이 아니라, 이미 실행된 명령의 부작용까지 고려해야 한다는 점을 배웠습니다.

요청 식별에는 Sequence만 사용하는 것보다 Type, Length, Payload까지 함께 비교하는 것이 안전하다는 점을 확인했습니다.

### 현재 한계

현재 Cache에는 가장 최근의 요청과 응답 한 쌍만 저장합니다.

여러 요청이 처리된 뒤 과거 요청이 다시 도착하면 새로운 요청으로 처리될 수 있습니다.

향후 다중 Transaction Cache와 유효 시간 관리 기능으로 확장할 수 있습니다.

---

## 3. 여러 Task의 UART 송신을 Single Writer 구조로 통합

### 문제 상황

센서 로그, 상태 출력, Text 명령 응답, Binary Protocol 응답 등 여러 Task에서 UART 송신이 필요했습니다.

각 Task가 직접 `HAL_UART_Transmit_DMA()`를 호출하면 다음 문제가 발생할 수 있었습니다.

- 이전 DMA 송신이 끝나기 전에 새로운 송신 요청 발생
- `HAL_BUSY`로 메시지 누락
- DMA 완료 전에 송신 Buffer 내용 변경
- Text 로그와 Binary 응답이 섞임
- DMA 완료 대기 코드가 여러 Task에 중복
- UART 상태 관리 책임이 여러 위치에 분산

### 원인 분석

UART DMA는 여러 Task가 동시에 독립적으로 사용할 수 있는 자원이 아닙니다.

한 Task가 DMA 송신 중일 때 다른 Task가 다시 송신을 시작하면 UART가 Busy 상태일 수 있습니다.

또한 DMA는 함수 호출이 끝난 뒤에도 Buffer를 계속 읽습니다.

```text
Task
→ 지역 Buffer 생성
→ DMA 송신 시작
→ 함수 종료 또는 Buffer 재사용
→ DMA가 변경된 데이터를 읽을 가능성
```

Mutex로 UART 접근을 보호할 수도 있지만, 각 Task가 DMA 완료까지 기다리면 송신 관리 코드가 여러 위치에 분산되고 Blocking 시간이 증가할 수 있다고 판단했습니다.

### 수정 방법

UART 송신 전용 `uartTxTask`를 만들었습니다.

다른 Task는 UART HAL 함수를 직접 호출하지 않고, 송신할 메시지를 `uartTxQueue`에 넣도록 구성했습니다.

```text
여러 Task
→ UartTxMessage_t 생성
→ uartTxQueue 등록
→ uartTxTask가 순서대로 수신
→ HAL_UART_Transmit_DMA 실행
→ DMA 완료 Callback
→ Direct Task Notification
→ 다음 메시지 송신
```

실제 UART DMA 호출 주체를 `uartTxTask` 하나로 제한해 Single Writer 구조를 적용했습니다.

### 송신 메시지 구조체

```c
#define UART_TX_MESSAGE_SIZE 160U

typedef struct
{
    uint16_t length;
    char data[UART_TX_MESSAGE_SIZE];
} UartTxMessage_t;
```

Queue는 구조체 전체를 복사하므로 호출한 Task의 지역 Buffer가 사라지거나 변경돼도 Queue에 복사된 데이터는 유지됩니다.

### DMA 완료 처리

DMA 완료 Callback에서는 복잡한 처리를 하지 않고 `uartTxTask`에 Direct Task Notification만 전달합니다.

```text
UART DMA 완료 ISR
→ uartTxTask Notification
→ uartTxTask 대기 해제
→ 다음 Queue 메시지 처리
```

ISR에서는 최소 처리만 하고 실제 후속 처리는 Task Context에서 수행했습니다.

### 적용 설정

- `uartTxQueue` 길이: 4개
- 메시지 최대 크기: 160Byte
- 실제 DMA 호출 주체: `uartTxTask`
- DMA 완료 전달 방식: Direct Task Notification
- Queue 등록 실패 시 `TX FAIL` 증가

### 데이터 흐름

```text
appTask
commandTask
monitorTask
Binary Protocol
→ uartTxQueue
→ uartTxTask
→ HAL_UART_Transmit_DMA
→ DMA 완료 Callback
→ Direct Task Notification
→ 다음 메시지 처리
```

### 검증 방법

- Text 로그와 Binary 응답을 함께 발생시켜 송신 순서 확인
- Python 자동 테스트 11개 실행
- 동일 테스트 10회 반복
- `PKT STAT`으로 TX Queue 등록 실패 횟수 확인
- FreeRTOS Heap과 Task Stack High Water Mark 확인

### 검증 결과

- 자동 테스트 총 110/110 통과
- UART TX Queue Failure 0회
- UART RX Drop 0회
- Binary Frame 응답 순서 정상 유지
- Free Heap 5,464Byte 유지
- Minimum Free Heap 5,464Byte 유지
- `uartTxTask` 최소 Stack 여유 160Word

```text
FREE HEAP     : 5,464Byte
MIN FREE HEAP : 5,464Byte
UART TX STACK : 160Word
TX FAIL       : 0
```

### 배운 점

공유 자원 문제는 항상 Mutex를 추가하는 방식으로만 해결할 필요는 없다는 점을 배웠습니다.

UART 송신처럼 요청 순서, Buffer 수명, 완료 이벤트를 함께 관리해야 하는 기능은 전용 Task 하나만 실제 하드웨어에 접근하도록 제한하는 Single Writer 구조가 책임 분리와 디버깅에 적합했습니다.

또한 DMA를 사용할 때는 함수 호출 시점뿐 아니라 DMA 완료 시점까지 Buffer가 유효해야 한다는 점을 배웠습니다.
