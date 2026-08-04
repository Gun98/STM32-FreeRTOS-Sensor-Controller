# 검증 및 테스트 결과

정상 요청뿐 아니라 잘못된 요청, 통신 오류, 센서 단선, Task 정지 상황을 직접 발생시켜 시스템의 처리와 복구 동작을 검증했습니다.

> GitHub 렌더링 오류를 피하기 위해 이 문서에서는 Markdown 표와 Mermaid를 사용하지 않습니다.

## 1. Binary Protocol 자동 테스트

Python과 `pyserial`을 이용해 다음 과정을 자동화했습니다.

```text
요청 Packet 생성
→ CRC 계산
→ UART 전송
→ MCU 응답 수신
→ 응답 Type, Sequence, Payload 검증
→ PASS 또는 FAIL 판정
```

### 테스트 1: PING

- 요청: `PING`
- 기대 결과: `PONG`
- 결과: PASS

### 테스트 2: 상태 조회

- 요청: `GET_STATUS`
- 검증 내용: 거리값과 센서 유효 상태가 포함된 `STATUS` 응답
- 결과: PASS

### 테스트 3: LED ON

- 요청: `LED_SET`
- Payload: LED ON 값
- 검증 내용: Binary 명령을 통한 LED ON
- 결과: PASS

### 테스트 4: LED OFF

- 요청: `LED_SET`
- Payload: LED OFF 값
- 검증 내용: Binary 명령을 통한 LED OFF
- 결과: PASS

### 테스트 5: 잘못된 LED Payload

- 요청: `LED_SET`
- Payload: 허용되지 않은 값
- 기대 결과: `INVALID_PAYLOAD`
- 결과: PASS

### 테스트 6: 정의되지 않은 Type

- 요청: 정의되지 않은 Packet Type
- 기대 결과: `UNKNOWN_TYPE`
- 결과: PASS

### 테스트 7: 응답 유실 및 Retry

- 첫 번째 응답을 PC 테스트 코드에서 의도적으로 무시
- 같은 Sequence로 요청 재전송
- 최대 Retry 횟수: 3회
- 결과: PASS

### 테스트 8: 동일 요청 재전송

- Version, Type, Sequence, Length, Payload가 같은 요청 재전송
- 검증 내용: 명령을 다시 실행하지 않고 Cached Response 재전송
- 결과: PASS

### 테스트 9: 동일 Sequence와 다른 Payload

- Sequence는 동일
- Payload는 다르게 전송
- 검증 내용: 중복 요청이 아닌 새로운 요청으로 처리
- 결과: PASS

### 테스트 10: 잘못된 CRC 이후 복구

- CRC가 손상된 Packet 전송
- 손상 Packet 폐기 확인
- 다음 정상 Packet 처리 확인
- 결과: PASS

### 테스트 11: 불완전 Packet Timeout 이후 복구

- SOF와 Packet 일부만 전송
- 100ms Parser Timeout 확인
- 다음 정상 Packet 처리 확인
- 결과: PASS

### 반복 테스트 결과

```text
11개 테스트 × 10회 반복
총 결과: 110/110 PASS
```

## 2. 통신 오류 주입 결과

10회 반복 테스트 후 누적 통계:

```text
VALID 140 | DUPLICATE 20
CRC ERROR 10 | TIMEOUT 10 | RX DROP 0 | TX FAIL 0
```

각 통계의 의미:

- `VALID 140`: CRC 검증을 통과해 Handler까지 전달된 Packet 140개
- `DUPLICATE 20`: 동일 요청으로 판단돼 명령을 재실행하지 않은 Packet 20개
- `CRC ERROR 10`: CRC 불일치 Packet 10개 감지
- `TIMEOUT 10`: 완성되지 않은 Packet 10개 Timeout 처리
- `RX DROP 0`: Stream Buffer 저장 실패로 유실된 수신 Byte 없음
- `TX FAIL 0`: UART TX Queue 등록 실패 없음

## 3. CRC 오류 검증

CRC가 잘못된 Binary Packet을 전송했습니다.

```text
손상 Packet 수신
→ CRC 불일치 검출
→ Packet Handler 실행하지 않음
→ 명령 실행하지 않음
→ CRC ERROR 증가
→ Parser 초기화
→ 다음 정상 Packet 처리
```

검증 결과:

- 손상 Packet 10개 모두 감지
- 손상 명령 실행되지 않음
- CRC 오류 이후 정상 Packet 처리 성공

## 4. Parser Timeout 검증

SOF와 Header 일부만 전송한 뒤 나머지 데이터를 보내지 않았습니다.

```text
부분 Packet 수신
→ 100ms 동안 Packet 미완성
→ Parser Timeout 발생
→ 부분 Packet 폐기
→ Parser 초기화
→ 다음 정상 Packet 처리
```

검증 결과:

- 불완전 Packet 10개 모두 Timeout 처리
- Timeout 이후 정상 Packet 처리 성공

## 5. 중복 요청 검증

첫 번째 응답을 PC에서 의도적으로 무시하고 동일 요청을 재전송했습니다.

```text
첫 번째 요청
→ MCU 명령 실행
→ 응답 생성 및 Cache 저장
→ PC에서 첫 응답 무시

Retry 요청
→ 동일 요청 감지
→ 명령 재실행하지 않음
→ Cached Response 재전송
```

검증 결과:

- 중복 요청 20개 감지
- 명령 중복 실행 방지
- 기존 응답 재전송 성공
- 같은 Sequence라도 Payload가 다르면 새로운 요청으로 처리

## 6. 센서 단선 및 자동 복구 테스트

HC-SR04 ECHO 신호선을 분리해 센서 입력 이상을 만들었습니다.

```text
ECHO 단선
→ 거리 측정값 Invalid 판정
→ 센서 상태 이상 표시
→ 다른 Task와 UART 명령은 계속 동작
→ ECHO 재연결
→ 정상 거리 측정 자동 복구
```

검증 결과:

- 센서 단선 감지 성공
- 전체 시스템은 계속 동작
- 재연결 후 별도 Reset 없이 자동 복구

## 7. Watchdog 복구 테스트

다음 디버그 명령으로 `appTask`를 정지시켰습니다.

```text
FAULT WATCHDOG
```

실제 처리 흐름:

```text
FAULT WATCHDOG 입력
→ appTask 정지
→ HEALTH_APP 미보고
→ watchdogTask가 Health 조건 불충족 감지
→ IWDG Refresh 중단
→ IWDG Reset 발생
→ 시스템 재부팅
→ RESET CAUSE: IWDG 출력
```

검증 결과:

- `appTask` 정지 감지
- IWDG Refresh 중단
- Watchdog Reset 발생
- 재부팅 후 Reset 원인 확인

```text
RESET CAUSE: IWDG
```

## 8. 부팅 Self-Test

정상 부팅 시 출력:

```text
=== SYSTEM SELF TEST ===
[SELF TEST] RTOS OBJECTS : PASS
[SELF TEST] OLED 0x3C    : PASS
[SELF TEST] EEPROM 0x57  : PASS
[SELF TEST] RTC 0x68     : PASS
[SELF TEST] SENSOR       : PASS
[SELF TEST] RESULT       : PASS
========================
```

검사 항목:

- RTOS Objects: Queue, Event Flag, Stream Buffer 등 주요 RTOS 객체 생성 여부 확인
- OLED: I2C 주소 `0x3C` 응답 확인
- EEPROM: I2C 주소 `0x57` 응답 확인
- RTC: I2C 주소 `0x68` 응답 확인
- Sensor: 유효한 Sensor Snapshot 준비 여부 확인

센서 데이터가 아직 준비되지 않은 경우에는 즉시 고장으로 판단하지 않고 `WAIT` 상태로 구분했습니다.

## 9. Heap 안정성 확인

반복 테스트 후 결과:

```text
FREE HEAP     : 5,464Byte
MIN FREE HEAP : 5,464Byte
```

현재 Free Heap과 Minimum Free Heap이 동일하게 유지됐습니다.

이 결과는 반복 테스트 중 Heap이 지속적으로 감소하지 않았음을 보여줍니다.

모든 종류의 메모리 오류가 없다고 단정하는 값은 아니며, 현재 반복 시험 범위에서 Heap 감소 현상이 없었음을 확인한 결과입니다.

## 10. Task Stack 측정 결과

```text
appTask       : 330Word / 1,320Byte
heartbeatTask :  96Word /   384Byte
consumerTask  :  76Word /   304Byte
eventTask     :  24Word /    96Byte
monitorTask   :  22Word /    88Byte
commandTask   :  99Word /   396Byte
watchdogTask  :  58Word /   232Byte
uartTxTask    : 160Word /   640Byte
```

검증 결과:

- 현재 기능에서 Stack Overflow 발생하지 않음
- `eventTask`와 `monitorTask`의 Stack 여유가 상대적으로 작음
- 기능 추가 시 두 Task의 High Water Mark를 우선 재측정할 필요가 있음

## 11. 최종 검증 결과

```text
Binary Protocol 자동 테스트     : 110/110 PASS
PING 및 상태 조회               : PASS
LED ON/OFF 제어                  : PASS
잘못된 Payload 처리             : PASS
정의되지 않은 Type 처리         : PASS
CRC 오류 검출                   : 10회 정상 감지
CRC 오류 이후 Parser 복구       : PASS
불완전 Packet Timeout           : 10회 정상 감지
Timeout 이후 Parser 복구        : PASS
중복 요청 실행 방지             : 20개 정상 감지
동일 Sequence, 다른 Payload     : 새로운 요청으로 정상 처리
UART RX Drop                    : 0회
UART TX Queue Failure           : 0회
센서 ECHO 단선 감지             : PASS
센서 재연결 자동 복구           : PASS
IWDG 자동 Reset                 : PASS
Reset 원인 확인                 : RESET CAUSE: IWDG
부팅 Self-Test                  : 전체 PASS
반복 시험 후 Heap 감소          : 없음
Stack Overflow                  : 발생하지 않음
```

## 12. 검증을 통해 확인한 점

- 손상 Packet은 명령 실행 전에 차단됨
- 불완전 Packet은 Timeout 후 폐기됨
- 응답 유실 시 동일 Sequence로 Retry됨
- 중복 요청은 명령을 다시 실행하지 않고 기존 응답만 재전송됨
- 센서 단선이 전체 시스템 정지로 이어지지 않음
- 주요 Task 정지 시 IWDG를 이용해 시스템이 재부팅됨
- 반복 테스트 후 Heap 감소와 UART Queue 오류가 발생하지 않음

단순히 기능이 한 번 동작하는 것을 확인하는 데서 끝내지 않고, 오류를 직접 주입하고 측정값을 기록해 펌웨어의 안정성과 복구 동작을 검증했습니다.
