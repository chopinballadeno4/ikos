# SimplifyUpcastComparisonPass 수정 사항

모든 패턴은 임의의 비트 폭 N, M에 대해 동작 (`type->bit_width()` 기반, 32비트 고정 아님).

---

## 지원 패턴

```
(1) x: si_N  →  sext  →  y: si_M
    y silt/sige C  →  insert: x silt/sige C

(2) x: ui_N  →  zext  →  y: ui_M
    y uilt/uige C  →  insert: x uilt/uige C

(3) x: ui_N  →  zext  →  t: ui_M  →  bitcast  →  y: si_M
    y silt C  →  insert: x uilt C          (predicate flip: SI→UI)

(4) x: si_N  →  bitcast  →  y: ui_N
    y uilt/uile C  →  insert: x sige 0, x silt/sile C   (predicate flip: UI→SI)

(5) x: si_N  →  sext  →  t: si_M  →  bitcast  →  y: ui_M
    y uilt/uile C  →  insert: x sige 0, x silt/sile C   (predicate flip: UI→SI)

(6) x: si_N  →  bitcast  →  t: ui_N  →  zext  →  u: ui_M  →  bitcast  →  y: si_Mw
    y silt/sile C  →  insert: x sige 0, x silt/sile C   (flip 없음: SI→SI)
```

---

## 안전 조건 (공통)

- C ≤ 2^(N-1) − 1  (원본 타입의 양수 범위 안)
- `run_on_upcast` 가 `IntInterval::top(N, sign).contains(C)` 로 자동 검사

## Predicate 제한

signed 원본(`is_signed()`)이 bitcast 체인에 있을 때:
- **패턴 4, 5**: 비교 변수가 unsigned → UILT/UILE만 허용
- **패턴 6**: 비교 변수가 signed → SILT/SILE만 허용
- GT/GE 계열 거부: `x < 0`도 참 분기를 만족하므로 `x sige 0`이 사운드하지 않음

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

---

## 미지원

```
x: ui_N  →  bitcast  →  y: si_N   │  y silt C
```
`silt C` 단독으로는 y ≥ 0 보장 없음 (y = -1도 만족). x 범위 추론 불가.
