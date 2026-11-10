# SimplifyUpcastComparisonPass 수정 사항

## 기존 지원 패턴

```
(1) x: si_N  →  sext  →  y: si_M              │  y silt C   →  insert: x silt C
(2) x: ui_N  →  zext  →  y: ui_M              │  y uilt C   →  insert: x uilt C
(3) x: ui_N  →  zext  →  t: ui_M
              →  bitcast  →  y: si_M           │  y silt C   →  insert: x uilt C
```

## 추가된 패턴

### (4) 순수 bitcast — si → ui

```
x: si_N  →  bitcast  →  y: ui_N               │  y uilt/uile C
```

**안전 조건**: C ≤ 2^(N-1) − 1

**삽입 assertion** (2개):
```
x sige 0        ← 하한
x silt/sile C   ← 상한 (predicate flip)
```

**근거**: y uilt C이고 C < 2^(N-1)이면 sign bit = 0 → 비트 패턴 동일 → x ∈ [0, C-1] as si_N.

---

### (5) sext → bitcast — si → si → ui

```
x: si_N  →  sext  →  t: si_M  →  bitcast  →  y: ui_M   │  y uilt/uile C
```

**안전 조건**: C ≤ 2^(N-1) − 1 (원본 타입 si_N 의 양수 범위 안)

**삽입 assertion** (2개):
```
x sige 0        ← 하한
x silt/sile C   ← 상한 (predicate flip: uilt → silt)
```

**근거**: y uilt C이고 C < 2^(M-1)이면 ui_M sign bit = 0 → t ≥ 0 as si_M → sext 역전파로 x ≥ 0 as si_N, 그리고 t = x (값 보존) → x < C.

## 미지원 케이스

### ui → si 방향 bitcast

```
x: ui_N  →  bitcast  →  y: si_N   │  y silt C
```

`silt C` 단독으로는 y ≥ 0 보장 없음 (y = -1도 만족). x 범위 추론 불가.

### uigt / uige / uieq / uine predicate (패턴 4, 5 한정)

`y uigt C` 참 분기에서 x < 0이어도 y = bitcast(sext(x))가 매우 큰 값이 되어 조건 만족 가능. `x sige 0` assertion이 사운드하지 않음.
