# SimplifyUpcastComparisonPass 수정 사항

모든 패턴은 임의의 비트 폭 N, M에 대해 동작 (`type->bit_width()` 기반, 32비트 고정 아님).

---

## 지원 패턴

### 원본 IKOS 지원 (v3.5 이전)

```
(1) x: si_N  →  sext  →  y: si_M
    y <pred> C  →  insert: x <pred> C      (predicate 제한 없음)

(2) x: ui_N  →  zext  →  y: ui_M
    y <pred> C  →  insert: x <pred> C      (predicate 제한 없음)

(3) x: ui_N  →  zext  →  t: ui_M  →  bitcast  →  y: si_M
    y <pred> C  →  insert: x <flipped-pred> C   (predicate flip: SI→UI)
```

### 수정으로 추가된 패턴

```
(4) x: si_N  →  bitcast  →  y: ui_N                              [v3.5.1]
    y uilt/uile C  →  insert: x sige 0, x silt/sile C   (predicate flip: UI→SI)

(5) x: si_N  →  sext  →  t: si_M  →  bitcast  →  y: ui_M        [v3.5.2]
    y uilt/uile C  →  insert: x sige 0, x silt/sile C   (predicate flip: UI→SI)

(6) x: si_N  →  bitcast  →  t: ui_N  →  zext  →  u: ui_M  →  bitcast  →  y: si_M   [v3.5.3+]
    y silt/sile C  →  insert: t uilt/uile C        (중간 삽입, predicate flip: SI→UI)
                       insert: x sige 0, x silt/sile C   (원본 삽입, flip 없음: SI→SI)
    t에 중간 삽입이 필요한 이유: ptrshift가 t를 직접 경유하는 경우
    (예: bitcast(t) → sext → 인덱스) 에서 t의 구간이 좁혀져야 전파 가능.

(7) x: ui_N  →  bitcast  →  y: si_N                              [v3.5.4+]
    y sigt/sige C (C >= 0)  →  insert: x uigt/uige C   (predicate flip: SI→UI)
    y > C (C >= 0) 이면 y 는 반드시 양수 → x = bitcast(y) 도 [C+1, 2^(N-1)-1] 범위 보장.
    LT/LE 는 음수 y 가 참 분기를 통과 → 큰 unsigned x 가 될 수 있으므로 미지원.
```

---

## 안전 조건 (공통)

- C ≤ 2^(N-1) − 1  (원본 타입의 양수 범위 안)
- `run_on_upcast` 가 `IntInterval::top(N, sign).contains(C)` 로 자동 검사

## Predicate 제한

| 패턴 | 원본 타입 | 허용 predicate | 이유 |
|------|-----------|---------------|------|
| 4, 5 | si_N (signed) | UILT, UILE, SILT, SILE | LT/LE 만 큰 값(음수 매핑)을 걸러냄 |
| 6    | si_N (signed) | SILT, SILE | 비교가 signed, 동일 이유 |
| 7    | ui_N (unsigned) | SIGT, SIGE | GT/GE 만 음수 y 를 걸러냄 |
| 1, 2, 3 | — | 제한 없음 | bitcast 없어 부호 혼용 없음 |

## Predicate Flip 규칙

`flip = (비교 predicate 부호) ≠ (원본 변수 타입 부호)`

| 패턴 | 비교 부호 | 원본 부호 | flip |
|------|-----------|-----------|------|
| 1    | S         | S         | ✗   |
| 2    | U         | U         | ✗   |
| 3    | S         | U         | ✓   |
| 4    | U         | S         | ✓   |
| 5    | U         | S         | ✓   |
| 6    | S         | S         | ✗   |
| 7    | S         | U         | ✓   |

---

## 미지원

```
x: ui_N  →  bitcast  →  y: si_N   │  y silt/sile C
```
참 분기에서 음수 y 도 통과 (y = -1 도 y < C 만족) → x = 65535 같은 큰 값이 될 수 있어 범위 추론 불가.
