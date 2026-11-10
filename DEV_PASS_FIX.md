# SimplifyUpcastComparisonPass 수정 사항

## 기존 지원 패턴

```
(1) x: si_N  →  sext  →  y: si_M   │  y silt C   →  insert: x silt C
(2) x: ui_N  →  zext  →  y: ui_M   │  y uilt C   →  insert: x uilt C
(3) x: ui_N  →  zext  →  t: ui_M
              →  bitcast  →  y: si_M │  y silt C   →  insert: x uilt C
```

## 추가된 패턴 — 순수 bitcast (si → ui)

```
x: si_N  →  bitcast  →  y: ui_N   │  y uilt/uile C
```

**안전 조건**: C ≤ 2^(N-1) − 1 (C가 si_N 양수 범위 안)

**삽입 assertion** (2개):
```
x sige 0       ← 하한
x silt/sile C  ← 상한 (predicate flip)
```

**근거**: ui_N은 항상 ≥ 0이므로 `y uilt C` 참 분기에서 y ∈ [0, C-1]. C가 안전 범위 안이면 비트 패턴 동일 → x ∈ [0, C-1] as si_N.

## 미지원 — ui → si 방향

```
x: ui_N  →  bitcast  →  y: si_N   │  y silt C
```

`silt C` 단독으로는 y ≥ 0 보장 없음 (y = -1도 만족). x 범위 추론 불가.
