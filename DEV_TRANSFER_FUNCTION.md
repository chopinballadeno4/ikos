# ikos_ar_parameter_relation_symbolic_transfer_spec.md

## 1. 목적

본 문서는 IKOS AR 기반 Parameter-Relation Symbolic Analysis에서 사용하는 symbolic state와 transfer/merge 규칙을 정의한다.

분석의 목적은 함수 내부의 AR statement와 CFG를 따라 symbolic state를 계산하고, 함수 종료 시 original parameter 기반의 종료 조건과 caller-visible memory state를 얻는 것이다.

caller에서 summary를 실제로 적용하여 normal flow를 pruning하거나 caller memory를 갱신하는 과정은 본 문서의 범위에 포함하지 않는다.


개정된 종료 보장 계약은 DEV_SUMMARY.md §1.1을 따른다. `C_X`는 충분조건이며
`[[P_memory ∧ C_X]] ∩ DefInputs ⊆ GuaranteedExitInputs_X`이다(P_memory가 없으면 true).
이 보장은 정의된 실행에 한정하며, C_X는 연산의 UB 부재를 인증하지 않는다.
Partial/Complete는 명시된 전제 안에서 구분하고 미확인 입력은
caller 분석에 남긴다. 이 문서는 목표 명세이며 현재 코드의 충족 여부는 별도다.

## 2. Symbolic State

분석 상태는 다음과 같다.

```text
Σ = (σ, μ, ρ)
```

별도의 pointer state `π`는 사용하지 않는다.

### 2.1 σ

```text
σ : V_SSA → Expr ∪ Loc ∪ {Null} ∪ BoolExpr ∪ {TOP}
```

`σ`는 AR SSA variable을 함수 entry 시점의 parameter-origin symbolic expression 또는 symbolic location에 대응시킨다.

예:

```text
σ(%1) = a₀
σ(%2) = L₁
σ(%3) = a₀ + 1
```

### 2.2 μ

```text
μ : Loc → Expr ∪ Loc ∪ {Null} ∪ BoolExpr ∪ {TOP}
```

`μ`는 symbolic memory location에 저장된 symbolic value를 나타낸다.

예:

```text
μ($3) = a₀
μ(L₁) = a₀ + 1
```

### 2.3 Symbolic location

```text
Loc ::= B | Loc(B, δ)
```

`δ`는 byte 단위의 parameter-origin affine symbolic expression이다.

```text
Loc(B, 0) = B
```

`PointerShift`가 다음과 같고:

```text
result = base + Σ(factorᵢ × operandᵢ)
```

`σ(base) = Loc(B, δ_base)`이며 모든 operand가 symbolic expression으로
환원 가능하면 다음과 같이 offset을 보존한다.

```text
δ_shift = Σ(factorᵢ × σ(operandᵢ))
σ(result) = Loc(B, δ_base + δ_shift)
```

하나의 operand라도 `TOP`이거나 affine symbolic expression으로 표현할 수 없으면:

```text
σ(result) = TOP
```

서로 다른 offset의 location은 서로 다른 memory location으로 취급한다.

```text
Loc(B, δ₁) ≠ Loc(B, δ₂)  if δ₁ ≠ δ₂
```

formal specification과 구현에서는 source-level parameter 이름에 의존하지 않는다.

따라서 `L_p`, `L_q`, `L_r`와 같은 표기는 설명용 예제에서만 사용할 수 있으며, 실제 구현에서는 AR parameter identity 또는 parameter index를 기준으로 symbolic location을 생성한다.

예:

```text
L₀
L₁
L₂
Loc(L₁, δ)
```


### 2.4 Typed pointer/null/Boolean value

Pointer value와 memory location은 DEV_SUMMARY.md §2.5와 동일하게 구분한다.
`P_i^0=PtrValue(L_i)`는 nullable entry pointer 값이며, 기존 Loc 표현을 재사용한다.
Null constant는 별도의 `Null` 값이다. `BoolExpr`는 offset 0인 pointer parameter의
IsNull/NonNull predicate에 대한 정확한 `{0,1}` 값 관계이다. Integer entry atom과
unsigned 1-bit parameter의 `{0,1}` range 정보도 보존한다.

Assignment/copy는 확장된 SymbolicValue를 전달한다. ZExt와 Boolean encoding을
보존하는 same-width bitcast는 origin을 유지한다. Truncation은 operand가 정확히
`{0,1}`이고 결과가 unsigned 1-bit일 때만 보존한다. SExt가 1을 -1로 바꾸는 경우나
range를 보장할 수 없는 truncation은 BoolExpr/Boolean origin을 TOP으로 만든다.
단순히 coefficient가 1인 parameter expression이라는 이유로 range를 가정하지 않는다.

### 2.5 조건 전용 origin 복원

`σ/μ`의 Expr는 affine 값 관계로 유지한다. 종료 조건에는 DEV_SUMMARY.md §2.6의
별도 typed CondExpr를 허용한다. `σ(r)=TOP`인 경우에도 r의 SSA 연산 출처를
정확히 확인하여 조건을 복원할 수 있으며, 두 표현 범위를 동일시하지 않는다.

```text
r = n srem k; σ(n)=n₀; k>0
    ⇒ RestoreOrigin(r)=SRem_w(n₀,k)
r = n urem k; σ(n)=n₀; k>0
    ⇒ RestoreOrigin(r)=URem_w(n₀,k)
r = n sdiv k 또는 udiv k
    ⇒ 대응하는 typed division CondExpr
r = x usub y
    ⇒ RestoreOrigin(r)=USub_w(RestoreOrigin(x),RestoreOrigin(y))
r = x umul y
    ⇒ RestoreOrigin(r)=UMul_w(RestoreOrigin(x),RestoreOrigin(y))
r == c ⇒ Compare(EQ, RestoreOrigin(r), RestoreOrigin(c))
r != c ⇒ Compare(NE, RestoreOrigin(r), RestoreOrigin(c))
```

Signedness, bitwidth, 정확한 cast/wrapping 의미와 정의 가능 조건을 함께 유지한다.
0 divisor 및 signed MIN/-1 연산을 임의의 정상 값으로 취급하지 않는다. Signed
나머지는 unsigned 나머지나 수학적 modulo로 바꾸지 않는다. 단일 연산의 복원이
가능하다는 사실만으로 정상 종료 전체가 보장되지는 않는다.

Copy/순수 연산의 정의를 재귀적으로 추적하며 관련 reaching definition을 모두
검증한다. ITE는 양쪽 값과 guard의 정확한 origin이 있을 때만 사용한다. 순환 정의,
모호한 load/alias, side effect 또는 미지원 연산은 Unrepresentable이다. 복원 비용
제한에 도달한 경우도 같은 실패 규칙을 따른다. SSA-origin 조회는 Σ에 새 pointer
state나 일반적인 path-condition component를 추가하는 것이 아니다.

#### 2.5.1 분기별 reaching definition을 비교 조건으로 치환

29번의 acyclic 조건부 대입은 필수 지원 범위다. 비교 operand의 origin을 단일 값으로
복원할 수 없어도, 각 reaching definition과 그 대입을 선택하는 guard를 복원한다.
해당 use에 이르는 definition의 포괄성, guard의 정확성, 대입 이후 값 변경 부재를
확인한다. Loop-carried/모호한 memory definition은 이 제한적 규칙으로 추측하지 않는다.

```text
RestoreCases(aligned) = {
    (URem_w(idx₀,2) u== 0, idx₀),
    (URem_w(idx₀,2) u!= 0, USub_w(idx₀,1))
}

RestoreComparison(v op k)
    = OR { G_j AND Compare(op,e_j,RestoreOrigin(k))
           | (G_j,e_j) in RestoreCases(v) }
```

이때 k가 상수인 29번의 비교는 반드시 복원한다. 짝수 guard에 홀수 대입식을
붙이지 않는다. 내부적으로 ITE를 사용해도 최종 조건은 이 분기별 DNF로 전개한다.
σ의 flat merge 자체를 guarded 값 domain으로 바꿀 필요는 없다.

`RestoreComparison`의 결과는 단일 Atom에 한정되지 않고 Condition/DNF일 수 있다.
수집한 조건에는 `C := C AND RestoreComparison(cmp)`로 합성한다. OR 결과를 atom
하나처럼 취급하거나 두 대안을 AND로 붙이지 않는다. 기존 경로 guard와 결합하면서
동일 atom을 중복 제거하거나 모순인 조합을 제거할 수 있다. 마지막 검사를 단순히
true로 바꾸거나 parity·USub 조건을 중복이라는 이유로 삭제하지 않는다.

#### 2.5.2 변수끼리의 typed multiplication

30번에서는 두 operand가 모두 변수여도 `UMul_32(step₀,idx₀)`를 종료 조건에
복원한다. σ의 affine multiplication처럼 상수 operand나 no-wrap flag를 요구하지
않는다. 이는 조건 전용 표현이며 wrapping을 허용하는 uint32_t 의미를 보존한다.

```text
prod = step umul idx
prod u<= 9
    ⇒ Compare(ULE,UMul_32(step₀,idx₀),9)
```

기존 범위가 product<=9를 함의하더라도 이 atom을 저장·출력하는 summary에서
제거하지 않는다. 29/30번의 정확한 기준과 동치 단순화의 예외는
DEV_SUMMARY.md §5.3·5.4를 따른다. 복원 실패 시의 fallback은 안전성을 위한 것이며
필수 지원을 구현한 것으로 취급하지 않는다.

### 2.6 표현식 범위 transfer

`ρ : ExprKey ⇀ MachineIntInterval`과 typed 값 identity는 DEV_SUMMARY.md §2.7~2.10을
따른다. 현재 실제 flow 전체에서 성립하는 범위만 기록한다. σ/μ의 값 관계가 TOP인
식도 ρ의 key가 될 수 있다. 기존 IKOS numerical state A를 교체하지 않고 함께 정제한다.

```text
Key(v) = 검증된 순수 연산 origin의 정규화 key 또는 독립적인 ValueId(v)
Lookup(ρ,E) = ρ(E) if present, typed TOP otherwise
EvalRange(E,A,ρ) = SoundIntervalTransfer(E,A,ρ) ∩ Lookup(ρ,E)
Record(ρ,E,I): ρ(E) := Lookup(ρ,E) ∩ I
```

SoundIntervalTransfer는 해당 machine-integer 연산의 bitwidth, signedness, wrapping과
cast 의미를 보존한다. 미지원 transfer 결과는 TOP이며 임의의 좁은 범위를 만들지
않는다. Pure wrapping UAdd와 정수 copy는 초기 필수 범위다.

#### Assignment / arithmetic

```text
r = op(v1,...,vn):
    pre := snapshot(A,Σ)
    E := Key(op(v1,...,vn), pre)
    I := EvalRange(E, pre.A, pre.ρ)
    invalidate keys depending on any redefined result binding
    execute existing numerical and σ/μ transfer
    A := RefineInterval(A, r, I)
    if E still names the same operand values in the post-state:
        Record(ρ,E,Interval(A,r))
```

Copy는 원래 값의 정확한 key를 유지한다. 재대입으로 E의 operand identity 자체가
바뀌면 old-value 식을 현재 값의 식으로 저장하지 않는다. Loop-carried/다중 definition
등으로 정확한 origin을 구성하지 못하면 재사용을 포기하고 결과의 독립 ValueId와
sound numerical transfer만 사용한다. 재실행한 definition을 과거 실행과 혼동하지 않는다.

#### Comparison과 numerical refinement

```text
유효한 assume E op c 또는 Interval(A,v)의 정제, Key(v)=E:
    I := 해당 제약의 sound typed interval
    Record(ρ,E,I)
    A에서 현재 E와 값이 같다고 증명한 변수의 interval도 교차 정제
```

`E!=0`의 경우 unsigned interval의 하한이 0이면 1로 올릴 수 있다. Signed interval의
중간 값 제외를 한 interval로 저장하지 않는다. 범위 교차가 bottom이면 유효한 실제
flow 제약에 한해 flow를 제거한다. 초기화·UB 검사 상태를 interval 기록만으로
정상/안전으로 바꾸지 않는다. Numerical refinement 후에는 관련 표현식의 범위도
갱신하여, t1을 검사한 사실이 t1의 liveness 제거 전에 ρ에 들어가도록 한다.

#### Call / memory / liveness

Call 직전에 actual argument의 ExprKey와 의존 값 identity를 snapshot으로 보존한다.
Normal/exception flow에는 각각의 base ρ와 해당 flow에서만 유효한 제약을 사용한다.
Summary에서 얻는 범위는 DEV_PRUNING.md §7.6의 coverage/유효 영역 검사를 통과해야
한다. 부분 충분조건을 모든 반환 입력의 범위로 기록하지 않는다.

Store/unknown call이 바꿀 수 있는 mutable operand 및 memory에 의존하는 key는
무효화한다. 기존 σ/μ invalidation은 그대로 수행한다. 이미 읽어 고정된 scalar
ValueId는 그 값이 유지됨을 증명하면 memory write 뒤에도 사용할 수 있다. 별도의
load나 외부 call 결과를 이전 ValueId와 같다고 취급하지 않는다. 초기 범위는
메모리 주소를 직접 key로 쓰지 않는다.

Liveness의 forget은 값 변경과 다르다. r=E의 임시 r만 제거되어도 E의 의미와 입력
identity가 유효하면 ρ(E)를 유지한다. A에서 지워진 임시 r의 범위에만 의존하지
않으며 재계산한 변수에 ρ(E)를 반영한다. Key 수/깊이 제한에 따른 삭제는 TOP으로
정보를 잃는 방향만 허용한다.

#### Join / loop / checker

```text
(ρ1 ⊔ ρ2)(E) = Hull(Lookup(ρ1,E), Lookup(ρ2,E))
ρ1 ⊔ BOTTOM = ρ1
ρ_head_next(E) = IntervalWiden(ρ_head(E), ρ_candidate(E)) at widening steps
```

Key의 의미가 같은 incoming끼리만 결합하며 한 incoming에 없는 key는 TOP이다.
한쪽 경로의 range를 다른 경로에 복사하거나 interval을 교차하여 join하지 않는다.
PHI/reaching-definition 차이를 해소할 수 없으면 해당 key를 포기한다. 유한한 key와
interval widening을 기존 WTO lifecycle에 결합하고, 선택적인 narrowing은 sound한
재분석에 근거한다. 단순 flat MergeVal로 ρ의 loop 수렴을 가정하지 않는다.

모든 checker 사용 전에 A의 해당 값 interval과 ρ의 유효한 식 범위를 교차 정제한다.
따라서 22번의 t2는 기존 DBZ checker가 읽는 interval 자체가 [1,10000]이어야 한다.
별도 map에 저장만 하고 checker가 기존의 넓은 A를 읽는 구현은 완료로 보지 않는다.

### 2.7 기존 numerical state로의 안전한 역방향 정제

`E op c`를 summary에 보존한 뒤, 적용이 허용된 flow에서 얻은 결과 interval을
E의 피연산자에 역전파한다. 이는 parameter 치환과 구분하며, 기존 변수 state에
대한 `refine`만 수행한다. 입력 값을 새로 assign하거나 일반적인 관계 domain을
추가하지 않는다. 각 단계는 원래 범위와 교차하며, 가능한 실제 입력을 모두 포함한다.

```text
I := EvalRange(E,A,ρ) ∩ Lookup(ρ,E) ∩ requested_interval
Record(ρ,E,I)
E와 같은 값을 나타내는 현재 변수들의 A 범위도 I와 교차
I = BOTTOM이면 이 유효한 실제 flow는 BOTTOM
이후 아래 표에 따라 피연산자 범위를 교차 정제
```

`L`, `R`은 피연산자의 기존 interval, `I`는 결과 interval이다. `⊕`, `⊖`는 해당
width/sign의 **wrapping interval 연산**이다. No-wrap 연산에도 이 역산을 사용할
수 있다. 정의된 no-wrap 실행은 동일한 modular 등식을 만족하므로 해를 버리지 않는다.

| 표현식 | 피연산자에 적용할 필요조건 / 적용 조건 |
|---|---|
| 직접 변수, copy | 해당 변수와 같은 식을 나타내는 변수의 interval을 I와 교차 |
| `L + R` | `L := L ∩ (I ⊖ R)`, `R := R ∩ (I ⊖ L_old)` |
| `L - R` | `L := L ∩ (I ⊕ R)`, `R := R ∩ (L_old ⊖ I)` |
| `x * k`, `k * x` | k가 singleton이고 no-wrap이거나 현재 입력 범위에서 overflow가 없음을 증명한 경우 정수 ceil/floor 역산. k=1은 wrapping에도 적용. k=0은 x를 정제하지 않음 |
| `x / k` | k가 양의 singleton일 때 정수의 0 방향 버림을 고려한 역산. `DivExact`에도 같은 필요조건을 사용 |
| 동일 폭 sign cast, 확장 cast | 결과 범위를 원래 타입으로 cast하여 원래 입력 범위와 교차. bit 단위 round trip이 성립하는 경우만 사용 |
| 좁히는 cast | 높은 bit를 복원하지 않으며 피연산자 정제 생략 |
| remainder, shift, bitwise, 그 외 곱셈/나눗셈 | 초기 역방향 지원 범위 밖. 기존 결과 범위 및 ρ만 유지 |

상수 곱셈의 수학적 역산은 k>0에서 `[ceil(I.lb/k), floor(I.ub/k)]`이며 k<0이면
결과 부호와 bound 순서를 뒤집어 계산한다. 반드시 원래 타입 및 입력 범위에 먼저
clip한 뒤 MachineInt로 변환한다. Wrapping 곱셈에 이 수학적 공식을 그대로 쓰지 않는다.

양의 k로 나눈 결과가 `[l,u]`이면 0 방향 버림을 고려한 수학적 필요 범위는 다음과 같다.

```text
lower = l*k - (l <= 0 ? k-1 : 0)
upper = u*k + (u >= 0 ? k-1 : 0)
```

이를 원래 입력 interval과 교차한다. Unsigned에서는 음수 lower가 0으로 clip된다.
0/음수/미확인 제수는 이 역산을 적용하지 않는다. UB 및 초기화 검사는 별도로 유지한다.

Add/sub는 양쪽이 변수라도 기존 범위에 따라 projection할 수 있다. 예를 들어
unsigned `a-b∈[1,10]`, `b∈[1,3]`이면 `a∈[2,13]`을 얻지만 a/b가 모두 TOP이면
개별 범위가 그대로일 수 있다. 이는 `a-b` 관계 자체를 interval domain에 저장한다는
뜻이 아니다. 두 compound expression끼리의 비교는 기존 domain이 지원하는
직접 변수 비교로 표현되지 않으면 초기 적용 범위에서 생략한다.

재귀 reduction은 한 번에 최대 128개 식 node를 방문하고 한도 초과 시 남은 정제를
생략한다. 완전한 constraint solver나 정제 fixpoint를 요구하지 않는다. 해가 여러
interval로 나뉘면 sound hull/TOP을 허용하며, 한쪽 해만 선택하지 않는다.

28번을 `guard_offset_pos(n-1)`로 변경한 경우 `ρ(USub_32(n,1))=[1,1000]`에서
`A(n)=[2,1001]`을 얻어 DBZ warning이 없어야 한다. 원래 `n+1` 버전은 n=0도
허용하므로 warning을 유지해야 한다. 정제 실패는 원래 summary atom의 삭제나
충분조건 약화의 근거가 아니다.

## 3. CFG Symbolic Merge

여러 CFG predecessor의 보조 state가 합류하면 σ/μ는 pointwise flat merge,
ρ는 §2.6의 같은 의미의 key에 대한 interval join을 수행한다.

```text
Σ_merge = Σ₁ ⊔ₛ Σ₂
```

`σ`와 `μ`의 각 symbolic value에 대해 다음 규칙을 사용한다.

```text
MergeVal(v₁, v₂) =
    v₁     if v₁ = v₂
    TOP    otherwise
```

즉:

```text
동일한 symbolic value
→ 유지

서로 다른 symbolic value
→ TOP
```

예:

```text
σ₁(x) = a₀ + 1
σ₂(x) = a₀ + 1

→ σ_merge(x) = a₀ + 1
```

반면:

```text
σ₁(x) = a₀ + 1
σ₂(x) = b₀ + 2

→ σ_merge(x) = TOP
```

`μ`에도 동일한 규칙을 적용한다.

### 3.1 Missing entry

구현에서 `σ`와 `μ`는 sparse map으로 표현할 수 있다. 도달 가능한 state의 map에
key가 존재하지 않는 경우 해당 값은 `TOP`으로 해석한다.

```text
Lookup(M, k) =
    M(k)    if k ∈ dom(M)
    TOP     otherwise
```

따라서 pointwise merge는 두 map의 key 합집합에 대해 다음과 같이 계산한다.

```text
(M₁ ⊔ₛ M₂)(k)
= MergeVal(Lookup(M₁, k), Lookup(M₂, k))
```

예를 들어:

```text
M₁(k) = a₀
k ∉ dom(M₂)

→ MergeVal(a₀, TOP)
→ TOP
```

이는 도달 불가능한 state인 `BOTTOM`과 구분한다. `BOTTOM` predecessor는 merge에
기여하지 않으므로 다음 관계가 성립한다.

```text
BOTTOM ⊔ₛ Σ = Σ
```

이 기본 정책 전에 Section 3.2의 Boolean 생성 정규화를 수행한다. 그 외에는
path correlation을 보존하지 않으며 서로 다른 symbolic relation은 TOP으로 병합한다.
이 MergeVal 규칙은 σ/μ에 대한 것이다. ρ의 missing key 역시 typed TOP이지만
같은 key의 서로 다른 interval은 §2.6에 따라 hull로 결합한다.

### 3.2 증명 가능한 Boolean 생성 정규화

```text
branch G:   b=1
branch ¬G:  b=0
→ 두 assignment에서 σ(b)=BooleanValue(G)
→ 합류점에서 동일한 BoolExpr 유지
```

현재 G는 동일 pointer value와 null의 PEQ/PNE이다. DEV_SUMMARY.md §9의 전체 definition 검사, comparison/assignment-only block,
acyclic 구성, pointer 재정의 부재 및 모든 entry-to-use 경로의 definition 통과 조건을
모두 충족할 때만 적용한다. 복제된 branch도 모든 definition이 같은 G를 표현해야 한다. 반대 encoding은 BooleanValue(¬G)이다.
메모리 merge나 일반적인 loop-dependent predicate에는 이 예외를 확대하지 않는다.
조건이 불충분하면 상수 assignment와 기존 merge를 그대로 사용한다.


## 4. Branch 처리

IKOS AR에서는 branch condition이 명시적인 branch statement로 표현되지 않고 successor basic block의 `Comparison`으로 나타날 수 있다.

예:

```text
#1 successors={#2, #3}

#2 {
    %8 sigt 0
    ...
}

#3 {
    %8 sile 0
    ...
}
```

IKOS analyzer는 각 successor에 해당 comparison constraint를 반영한 별도의 abstract state를 유지한다.

Symbolic analysis는 각 basic block별 `σ`, `μ` 상태를 유지하고 CFG 합류점에서 Section 3의 symbolic merge를 수행한다.


## 5. LLVM PHI의 IKOS AR 표현

LLVM PHI는 IKOS AR에서 별도의 PHI statement로 유지되지 않을 수 있다.

실제 AR에서는 각 predecessor에서 동일한 AR variable에 assignment하는 형태로 나타난다.

예:

```text
#2 {
    ...
    si32 %.0 = %4
}

#3 {
    ...
    si32 %.0 = %6
}

#4 {
    ... %.0 ...
}
```

따라서 predecessor별 상태가 다음과 같다면:

```text
#2:
σ(%.0) = a₀ + 1

#3:
σ(%.0) = b₀ + 2
```

#4 진입 시 CFG merge를 적용한다.

```text
σ(%.0)
= MergeVal(a₀ + 1, b₀ + 2)
= TOP
```

일반 PHI는 CFG merge로 처리한다. Boolean 생성 PHI는 Section 3.2의 증명된 관계로 assignment를 정규화한 뒤 CFG merge를 적용한다.


## 6. Loop 처리

IKOS는 WTO(Weak Topological Ordering)를 이용하여 loop를 식별하고 fixpoint를 계산한다.

예:

```text
#1 → #2
     ↓
     #3
     ↓
     #2

#2 → #4
```

여기서:

```text
#2 = loop head
#1 → #2 = loop-entry edge
#3 → #2 = back-edge
#2 → #4 = loop-exit edge
```


### 6.1 최초 loop-head state

최초 loop-head symbolic state는 loop-entry edge의 상태만 사용한다.

```text
Σ_H^0 =
    ⊔ₛ {
        Σ_OUT(P) |
        P → H is a loop-entry edge
    }
```

최초 상태 계산에서는 아직 back-edge를 반영하지 않는다.

예:

```text
#1 → #2
#3 → #2
```

이면:

```text
Σ_#2^0 = Σ_OUT(#1)
```


### 6.2 Back-edge 반영

loop body를 분석한 후 entry edge와 back-edge의 symbolic state를 merge한다.
σ/μ는 flat merge, ρ는 interval join을 사용하고 재실행한 binding의 의존 key를
무효화한다. 실제 loop head 갱신에는 §6.3의 WTOIntervalUpdate를 사용한다.

```text
Σ_candidate^(k+1)
    = Σ_entry ⊔ₛ Σ_back^k
```

예:

```text
entry:
σ(%.0) = n₀

back-edge:
σ(%.0) = n₀ + 1
```

이면:

```text
σ_candidate(%.0)
= MergeVal(n₀, n₀ + 1)
= TOP
```


### 6.3 Symbolic loop fixpoint

symbolic loop state는 IKOS의 기존 WTO iteration lifecycle과 동일한 basic block 방문 순서에서 반복적으로 갱신한다.

명세 수준에서는 다음과 같이 정의한다.

```text
Σ_H^0 =
    merge of loop-entry states
```

반복:

```text
Σ_candidate^(k+1)
    = Σ_entry ⊔ₛ Σ_back^k
```

```text
Σ_H^(k+1)
    = (FlatJoin(σ_H^k, σ_candidate^(k+1)),
       FlatJoin(μ_H^k, μ_candidate^(k+1)),
       WTOIntervalUpdate(ρ_H^k, ρ_candidate^(k+1)))
```

WTOIntervalUpdate는 §2.6의 interval join/widening을 WTO 반복 단계에 맞게 수행한다.
Loop 재실행의 binding invalidation과 유한 key 제한을 함께 적용한다. σ/μ의 안정만으로
ρ의 수렴을 가정하지 않으며 다음 조건에서 전체 state가 안정되었다고 본다.

```text
Σ_H^(k+1) = Σ_H^k
```


### 6.4 Symbolic widening

σ/μ의 flat symbolic domain에는 별도의 widening을 정의하지 않는다.

σ/μ의 merge 규칙에서는 서로 다른 symbolic value가 즉시 TOP으로 상승한다.

```text
n₀
↓
MergeVal(n₀, n₀ + 1)
↓
TOP
↓
MergeVal(n₀, TOP)
↓
TOP
```

따라서 loop-carried symbolic relation은 빠르게 안정된다.

IKOS의 기존 numerical AbstractDomain은 기존 방식대로 join/widening을 수행하고, `σ`, `μ`는 자체 symbolic merge 규칙으로 수렴한다.

```text
IKOS AbstractDomain:
    join → widening → fixpoint → narrowing

Symbolic σ/μ:
    MergeVal → fixpoint

Expression ranges ρ:
    interval join → widening → fixpoint → sound narrowing (선택)
```

따라서 σ/μ의 별도 widening은 사용하지 않는다. ρ에는 §2.6의 interval widening을 적용한다.


### 6.5 Symbolic narrowing

σ/μ의 symbolic 값 관계에는 별도의 narrowing을 적용하지 않는다.

한 번:

```text
σ(x) = TOP
```

으로 추상화된 symbolic relation은 IKOS의 narrowing 단계에서 다시 구체적인 expression으로 복원하지 않는다.

```text
TOP → TOP
```

위 TOP 유지 규칙은 σ/μ에 대한 것이다. ρ에는 sound한 재분석에 근거한 interval
narrowing을 허용한다. 더 정밀한 σ/μ loop 관계 복원은 향후 확장으로 둔다.


### 6.6 Loop exit

loop fixpoint 이후 loop-exit successor의 `Comparison`은 일반 exit constraint와 동일한 방식으로 처리한다.

예:

```text
%.0 sige 10
```

이고:

```text
σ(%.0) = TOP
```

이면 SSA origin 복원을 시도한다. 이 역시 실패하면 Section 8에 따라 해당 종료
보장 영역의 인증을 생략하고 Partial로 표시한다. Numerical loop fixpoint가
수렴했다는 사실만으로 loop 종료성을 보장하지 않는다.

loop exit의 memory update도 기존 `σ`, `μ` transfer 규칙을 그대로 적용한다.

예:

```text
store %2, %.0
```

에서:

```text
σ(%2) = L₁
σ(%.0) = TOP
```

이면:

```text
μ(L₁) = TOP
```


## 7. Exit 분리

함수 종료는 다음 세 종류로 구분한다.

```text
normal_exit
exception_exit
program_exit
```

### Normal exit

정상적인 함수 return이다.

정상 종료 경로에서:

```text
C_normal
μ_normal_exit
```

을 생성한다.

### Exception exit

exception이 caller 방향으로 전파되는 종료이다.

예:

```text
Resume
```

exception exit에서는:

```text
C_exception
μ_exception_exit
```

을 생성한다.

### Program exit

`abort()` 이후 `unreachable` 등 프로그램 자체가 종료되는 경로이다.

program exit는 caller로 복귀하지 않으므로 caller-visible exit summary를 생성하지 않는다.


### 7.1 알려진 C++ 예외 runtime의 필수 지원

Summary 생성기는 아래 동작을 일반 unknown call과 구분한다. 이름이 비슷한 사용자
함수가 아니라 importer가 식별한 AR intrinsic identity로 판정한다. 지원 범위는
정수 scalar 및 검증 가능한 클래스 예외 객체, `catch (...)`, 검증된 cleanup/resume 및
rethrow다. 클래스 생성은 아래 §7.2의 문맥 분석/검증된 library 계약으로 지원한다.
RTTI selector를 이용한 typed catch 판정, 미확인 생성/정리 동작 및 §8의 Writable
전제로도 모델링하지 못하는 외부 메모리 접근은 Partial fallback을 유지한다.

| AR 동작 | 보장 조건과 flow 처리 |
|---|---|
| `LibcppAllocateException` | 양의 상수 크기의 fresh 객체를 생성. 객체의 주소 origin과 크기를 추적하고 재실행 시 과거 초기화를 무효화 |
| 예외 객체 초기화 | 할당 크기에 맞는 정수 scalar 초기화 또는 §7.2에 따라 검증된 클래스 생성자의 정상 완료. 일반 외부 pointer store의 검증을 생략하지 않음 |
| `LibcppThrow` | 검증된 객체 초기화·정확한 live 주소·global RTTI origin을 확인한 뒤 exception flow만 생성. Null 또는 확인된 함수 주소의 destructor를 기록하며 아직 실행하지 않음 |
| `LibcppRethrow` | active catch가 있는 경우 그 예외를 재전파. 새 normal-return 경로를 만들지 않음 |
| `LandingPad` / `ExtractElement` | 인증된 unwind edge에서만 예외 record를 생성. 객체 pointer와 selector에 동일한 예외 origin token을 부여 |
| `LibcppBeginCatch` | pending 예외에서 추출된 객체 pointer인지 확인하고 active catch로 전환 |
| `LibcppEndCatch` | 재throw는 소유 예외를 보존. 실제 파괴가 필요한 catch 종료는 기록된 destructor 계약에 따라 정상/프로그램 종료/미확인을 구분 |
| `InsertElement` / `Resume` | 동일 예외 token의 객체·selector 두 필드가 모두 구성되었는지 확인한 뒤 함수 밖의 exception exit로 수집 |
| `LibcppFreeException` | 추적 중인 객체의 정확한 base에 한하여 해제와 초기화 무효화 |

`InsertElement`의 초기 aggregate가 undef인 표준 cleanup 패턴은 허용하지만,
실제 사용되는 필드가 undef이거나 두 필드의 예외 origin이 다르면 인증하지 않는다.
일반적인 undef 연산을 허용한다는 뜻이 아니다. 재실행한 throw는 새로운 token으로
구분하며 pending/active catch 및 예외 record는 경로별 상태로 유지한다.
예외 객체의 주소는 fresh 할당 결과와 검증된 pointer copy/bitcast 및 범위 내 상수
offset shift로 복원한다. 실제 throw/free는 offset 0의 정확한 live base만 허용한다.
재할당하면 과거 실행의 alias를 무효화하고, throw 뒤에는 객체의 소유권이
runtime으로 넘어가므로 같은 raw pointer의 재throw/해제를 인증하지 않는다.

이 계약의 runtime 의미는 기존 IKOS AR libcpp 모델을 따른다. 예외 할당은 그 모델의
성공한 non-null 할당 동작을 사용하며, 모델에 없는 자원 고갈/숨겨진 runtime 종료를
새로 추측하지 않는다. 해당 동작까지 모델에 포함하는 경우에는 그 종료 가능성을
별도로 검증하지 않고 동일한 Complete 보장을 사용해서는 안 된다.

Ordinary `Call`의 알려진 callee가 예외를 보장하면 그 조건은 현재 함수의 exception
exit로 합성하고, normal 조건만 다음 statement로 진행시킨다. `Invoke`는 callee의
normal/exception 조건을 각각 해당 successor에만 적용한다. 알려진 intrinsic의
불가능한 edge는 탐색하지 않는다. Unknown call은 예외가 없다고 가정하지 않으며,
그 경로의 미확인 영역 때문에 coverage를 Partial로 낮춘다.
Ordinary Call로 전파되는 예외는 별도 CFG landing-pad block이 없어도 논리적인
exception summary로 저장·출력한다. 명시적 CFG node 부재를 예외 부재로 오해하지 않는다.

31번 `require_positive(n)`의 필수 결과는 AR 모델에서 다음과 같다.

```text
C_normal = n₀ > 0       (Complete)
C_exception = n₀ <= 0   (Complete)
```

함수 내부에서 잡고 반환하면 해당 경로는 C_normal에 속하고 C_exception에는 넣지
않는다. 재전파하면 C_exception에 속한다. `abort`는 어느 조건에도 넣지 않는다.
알려진 runtime call이라는 이유만으로 관련 없는 unknown call까지 건너뛰지 않는다.

### 7.2 클래스 예외 생성과 정리의 인증

`throw Class(args...)`는 생성자 호출과 throw를 분리해서 분석한다. ABI에서 확인된
constructor identity와 formal/actual 타입을 사용하고, 이름 일부가 비슷하다는 이유로
임의 함수를 생성자로 인정하지 않는다. 본문이 있는 생성자는 fresh 예외 객체와
actual argument를 넘겨 제한적으로 문맥 분석한다. 중첩 생성자 16단계, 재귀 및
기존 경로 탐색 한도 초과는 Partial이다. 생성자가 정상 완료한 경우에만 해당 객체를
constructed로 기록한다. 생성자에서 나온 예외는 enclosing Invoke cleanup 또는
ordinary Call의 상위 handler로 전파하며, 미완료 객체의 constructed 상태를 제거한다.

Fresh 예외 객체의 정수 field와 확인된 pointer/vtable store는 상수 byte offset,
정의된 값, 할당 범위 및 정렬을 확인한다. 생성자 내부의 정수 field load는 그 경로의
확인된 store에서만 복원한다. 임의 외부 메모리 읽기/쓰기나 불명확한 alias를 이
규칙으로 허용하지 않는다. 정상 생성이 끝나도 미초기화 field를 읽을 수 있다고
가정하는 것은 아니다. 일반 copy/move, 미지원 aggregate 초기화는 보수적으로 남긴다.
Store와 byte 범위가 겹치는 과거 field 값은 무효화하며, load의 폭/sign이 기존
typed 값과 일치하지 않으면 복원하지 않는다. 객체가 해제되거나 소유권이 이전되면
생성 완료 기록만으로 throw를 인증하지 않고 live allocation을 다시 확인한다.

본문이 없는 library 생성자는 검증된 모델만 사용한다. 첫 모델은 Itanium ABI의
`std::logic_error`, `domain_error`, `invalid_argument`, `length_error`, `out_of_range`,
`runtime_error`, `range_error`, `overflow_error`, `underflow_error`의 `const char*`
생성자다. 정확한 declaration/constructor identity, this/char pointer/반환 타입,
live fresh storage 및 immutable global의 범위 내 NUL 종료 문자열을 확인한다.
모듈에 replacement allocation 정의나 사용된 new-handler 설치가 보이면 적용하지
않는다. 이 모델은 기존 AR allocator와 기본 표준 라이브러리 runtime 계약 아래서
정상 생성 또는 예외 전파를 허용하며, 둘 중 하나를 임의로 확정하지 않는다.
다른 ABI/overload, unknown 문자열, 사용자가 제공한 본문에는 이 모델을 강제하지 않는다.
모델 근거는 [C++ exception classes](https://eel.is/c++draft/std.exceptions)와 현재
toolchain의 libc++ `stdexcept` 생성자/소멸자 계약이다.

```text
생성자 결과가 nondeterministic normal 또는 exception이면:
Guarantee_X = Guarantee_X(normal continuation) ∧ Guarantee_X(exception continuation)
```

두 가능한 결과를 OR로 합쳐 유리한 경로만으로 종료를 인증하지 않는다. Normal
생성 뒤의 명시적 throw와 생성자 실패 cleanup/resume가 모두 외부 예외로 이어지면
그 공통 입력 영역은 C_exception으로 인증할 수 있다. 한 결과가 return/abort/미확인
동작으로 이어지면 같은 영역을 exception 보장으로 넣지 않는다. 가능한 결과의
종료가 다르면 해당 coverage를 Partial로 남긴다. Deterministic source branch들의
서로 다른 입력 영역은 기존대로 OR로 합친다.
Nondeterministic 결과를 교차할 때 DNF 조합은 1024개로 제한하고 한도 초과 시
해당 보장 기여를 버리고 Partial로 남긴다. 한 결과를 임의로 선택하지 않는다.

Destructor는 escaping throw 시 실행되지 않으므로, non-null destructor라는 이유만으로
그 exception exit을 버리지 않는다. 예외 token에 cleanup 계약을 별도로 기록한다.
Null destructor, 검증된 정상 destructor 및 위 표준 예외의 declaration 모델은 정상
cleanup이다. 본문 분석으로 abort가 확인되면 파괴 시 program termination으로 처리한다.
그 외는 unknown cleanup이다. Catch 종료에서 파괴가 필요할 때 unknown cleanup이면
그 이후 경로는 Partial이며, rethrow는 그 객체를 파괴하지 않고 다음 소유자로 넘긴다.
함수 간 exception summary로 cleanup 안전성을 전달할 때는 해당 component가 Complete인
경우만 전체 객체에 대한 정상 cleanup 계약을 재사용한다. 미분석 destructor를
noexcept라는 표기만으로 정상 반환한다고 가정하지 않는다.

Benchmark 41의 strict_check는 위 모델에서 C_normal = (0 < v <= 500),
C_exception = (v > 500)을 인증해야 한다. constructor 자체 throw, constructor abort,
unknown constructor, unknown/aborting destructor 및 nondeterministic mixed exit을
별도 regression으로 검증한다.

## 8. Exit Constraint 환원과 종료 보장 검증

각 경로의 AR integer/null comparison과 합성한 callee 보장 조건을 parameter-origin
조건으로 복원한다. 조건의 출처와 추출 시점을 명시하고, 이미 liveness로 사라진
제약을 최종 invariant에서 얻을 수 있다고 가정하지 않는다.

```text
Candidate_X,i = RestoreCondition(PathConditions_X,i, σ_i, SSAOrigins)
C_X,i = CertifyExit(Candidate_X,i, path/call/loop semantics)
```

`RestoreCondition`은 표현식 복원이며 `CertifyExit`는 종료 보장 검증이다. Abstract
exit invariant는 일반적으로 필요조건일 뿐이므로 그것을 충분조건으로 재명명하지
않는다. 경로 선택, 값 관계/effect, callee 보장과 필요한 loop 종료성이 해당 영역의
모든 정의된 실행에 대해 확인되어야 한다. 외부 call의 정상 반환이나 예외 부재도
근거 없이 가정하지 않는다.

포인터 parameter의 offset 0에 formal pointee와 타입이 일치하는 정수 scalar를
non-volatile store하는 경우, 그 store를 무시하지 않고 DEV_SUMMARY.md §3의
`Writable` 전제를 함수 공통 P_memory에 추가한 뒤 인증을 계속한다. 지원되는
정의된 scalar 값만 저장하며, 해당 전제 아래서 normal/exception의 caller-visible
μ에 최종 값을 보존한다. Unknown offset, 타입 불일치, volatile 및 미지원 외부
load는 Partial로 남긴다. 전제를 출력하지 않은 무조건부 Complete로 내보내지 않는다.

Callee 합성 시 메모리 전제도 actual argument에 치환한다. Actual이 현재 proof
경로에서 할당한 타입 일치 scalar local이고 offset 0 및 정렬이 확인되면 전제를
해소한다. 다른 formal pointer에 대응하면 wrapper의 P_memory로 전파한다. 그 외는
인증 실패이며, C_X만 가져와 전제를 누락해서는 안 된다. Local allocation은 한
객체인 경우만 인증하고, 미지원 호출 뒤에도 살아 있다고 추측하지 않는다.

기본 증명 구성은 DEV_SUMMARY.md §5.2를 따른다. Acyclic guard의 return/다른
terminal, 순수 assignment 및 branch에 대해 정의된 실행의 typed 의미와 backward 충분조건을
구성한다. 미지원 call/loop의 효과나 종료성을 생략한 채 인증하지 않는다. 기존
forward transfer/fixpoint는 값 관계 추적과 보조 증명에 사용하며 종료 보장 검증을
자동으로 대체하지 않는다.

### 8.1 SubstConstraint / RestoreCondition

값 치환은 DEV_SUMMARY.md §5와 같이 검증된 `σ` affine 관계를 먼저 사용하고,
지원하지 못하는 관계에 typed SSA origin을 사용한다. 동일 result width의 modular
affine 등식으로 값 동치를 확인할 수 있지만, cast/비선형 노드를 그 경계 너머의
수학적 선형식으로 간주하지 않는다. 계수의 host 정수 overflow는 TOP fallback이다.
No-wrap 연산의 값 관계는 정의된 실행 안에서 해석한다. Add/sub/mul의 overflow를
배제하기 위한 두 배 width atom은 생성하지 않는다. `C_X`와 Complete를 이용하여
해당 연산의 안전성을 소급 인증하지 않으며 native transfer/overflow checker는 유지한다.
No-wrap의 overflow 가능성만으로 coverage를 낮추지 않고, 정의된 실행의 exit 포괄성을
검증한다. 소스에 명시된 범위 조건, 나눗셈/나머지 및 29/30번 검사는 그대로 보존한다.
Unsigned wrapping이나 truncation의 의미를 제거하는 규칙도 아니다. 필요한 의미를
복원하지 못하면 기존 Partial fallback을 따른다.

```text
Atom ::= Compare(predicate, CondExpr, CondExpr)
       | IsNull(P_i^0) | NonNull(P_i^0)
CondExpr ::= DEV_SUMMARY.md §2.6의 typed 조건 표현
PEQ(pointer, null) 또는 PEQ(null, pointer) → IsNull(EvalPointer(pointer))
PNE(pointer, null) 또는 PNE(null, pointer) → NonNull(EvalPointer(pointer))
BooleanValue(G) == 1 또는 != 0 → G
BooleanValue(G) == 0 또는 != 1 → NOT G
NOT IsNull(P) → NonNull(P)
NOT NonNull(P) → IsNull(P)
```

Pointer origin은 offset 0인 외부 parameter를 지원한다. Null 상수 간 비교는
true/false로 계산한다. BoolExpr의 부정은 exact relation일 때만 사용하며 unknown의
부정으로 false를 만들지 않는다. 지원하지 않는 pointer 비교를 수치 비교로 바꾸지
않는다. `%`와 `/` 결과의 비교는 §2.5로 복원하며 affine σ만 조회하고 버리지 않는다.

환원 실패 시 다음 순서를 따른다.

```text
정확한 origin 복원 가능 → 원래 atom 유지
원래 조건을 함의하는 더 강한 입력 영역을 증명 가능 → 그 영역의 종료 보장 검증
그 외 → 해당 전체 conjunction을 인증하지 않고 coverage=Partial
결과를 신뢰할 수 없음 → Unavailable
```

예를 들어 `(a₀>0) AND (x>=10)`에서 x의 출처를 모르고 대체 증명도 없으면
`x>=10`을 true로 바꾸어 `C_X=a₀>0`으로 만들지 않는다. 다른 보장 영역도 없으면
`Known(false, Partial, ∅)` 또는 Unavailable이다. 이 false는 flow 불가능을
뜻하지 않는다. `Known(false, Complete, ∅)`에는 별도의 불가능성 증명이 필요하다.
`C_X=true`는 모든 허용 입력에서 해당 종료를 보장할 때만 허용한다.

조건을 제거해도 나머지 조건과 동치임을 증명한 경우에는 제거할 수 있다. 추측에
의한 atom 삭제나 interval hull로 충분조건의 영역을 넓히는 것은 금지한다.
29/30번에는 DEV_SUMMARY.md §5.3·5.4의 검사 보존 규칙이 우선한다. 동일 atom의
중복 제거는 가능하지만 parity·분기별 USub 비교·UMul 비교를 함의 관계에 의해
제거하지 않는다. 29번 비교의 복원 결과는 §2.5.1처럼 DNF일 수 있다.

### 8.2 Multiple exits와 coverage

```text
C_X = OR { C_X,i | 종료 보장이 인증된 영역 i }
```

인증된 disjunct만 합치며 전체 flow의 coverage는 독립적으로 증명한다. 경로를 일부
누락해도 충분조건은 유지할 수 있으나 기본 결과는 Partial이다. 누락한 경로가
불가능하거나 이미 조건에 포함된다는 증거가 있으면 Complete를 증명할 수 있다.
같은 입력에서 서로 다른 종료가 가능한 nondeterministic 경로는 한 경로의 존재만으로
종료 보장으로 인증하지 않는다. Benchmark 21의 정확한 DNF는 DEV_SUMMARY.md §5.1을
따른다.


## 9. Function Summary

callee `g`의 summary는 다음과 같이 구성한다.

```text
FunctionSummary(g) = {
    P_memory(g),
    NormalSummary(g),
    ExceptionSummary(g)
}
```

현재 return value summary는 생성하지 않는다.


## 9.1 Normal Summary

callee `g`의 normal summary는 최소한 다음으로 구성한다.

```text
NormalSummary(g) =
    (C_normal(g), Coverage_normal(g), μ_normal_exit,g)
```

`C_normal(g)`은 callee가 정상적으로 caller에 복귀함을 보장하는 parameter-based 충분조건이다.

`μ_normal_exit,g`은 정상 종료 시 caller-visible external memory의 symbolic 상태를 나타낸다.

현재 globals를 제외하므로 주 대상은 pointer/reference parameter로부터 도달 가능한 parameter-relative location이다.


### 9.2 Exception Summary

exception summary는 다음과 같이 구성한다.

```text
ExceptionSummary(g) =
    (C_exception(g), Coverage_exception(g), μ_exception_exit,g)
```

`C_exception(g)`은 exception exit 도달을 보장하는 parameter-based 충분조건이다.

`μ_exception_exit,g`은 exception exit 시점의 caller-visible external memory symbolic state이다.
두 flow 모두 μ relation은 적어도 C_X 내부의 모든 해당 종료 실행에서 성립해야 한다.
Partial의 memory effect를 미확인 입력에 무조건 적용하지 않는다. Known/Unavailable 및
coverage의 의미는 DEV_SUMMARY.md §3과 동일하다.


## 9.3 Exit Memory Filtering

exit 시점의 전체 symbolic memory state `μ_exit`에는 callee 내부 `Allocate`로 생성된 local memory location과 caller-visible external location이 함께 존재할 수 있다.

callee summary에는 caller에서 관찰 가능한 location만 포함한다.

```text
ExternalLoc
= {
    L ∈ dom(μ_exit) |
    L is caller-visible
  }
```

따라서 normal exit에서는:

```text
μ_normal_exit
= μ_exit | ExternalLoc
```

exception exit에서는:

```text
μ_exception_exit
= μ_exit | ExternalLoc
```

여기서:

```text
μ_exit | ExternalLoc
```

은 `μ_exit`을 `ExternalLoc`에 제한한 mapping이다.


### 9.4 Callee-local Allocate filtering

callee 내부 `Allocate`로 생성된 local location은 함수 종료 후 caller에서 관찰할 수 없으므로 summary에서 제거한다.

```text
L generated by current callee Allocate
→ L ∉ dom(μ_normal_exit)
→ L ∉ dom(μ_exception_exit)
```

반면 pointer/reference parameter로부터 도달 가능한 parameter-relative location은 caller-visible이면 유지한다.

formal notation에서는 parameter index/identity 기반 location을 사용한다.

예:

```text
L₀
L₁
Loc(L₀, δ)
Loc(L₁, δ)
```

caller-visible이면 exit memory summary에 유지한다.


### 9.5 Exit memory filtering 예제

예를 들어:

```text
μ_exit = {
    $3 ↦ a₀,
    $4 ↦ L₁,
    L₁ ↦ a₀ + 1
}
```

이고 `$3`, `$4`가 현재 callee의 `Allocate`에서 생성된 local location이면:

```text
μ_normal_exit = {
    L₁ ↦ a₀ + 1
}
```

이 된다.

구현에서는 `$` 문자 자체를 기준으로 local location을 판정하지 않는다.

다음과 같이 location의 origin을 기준으로 판단한다.

```text
origin(L) = current-callee Allocate
→ callee-local
```

즉 AR variable/location 이름의 문자열 형식은 filtering의 의미론적 기준이 아니다.


## 10. CFG Merge와 Summary 정밀도

현재 CFG merge는 다음 정책을 사용한다.

```text
same symbolic value
→ preserve

proved nullity Boolean construction
→ normalize assignments to the same BoolExpr

different symbolic value (otherwise)
→ TOP
```

따라서 branch 또는 loop에서 서로 다른 symbolic relation이 합류하면 이후 exit summary의 일부 값이 `TOP`이 될 수 있다.

예:

```text
branch 1:
μ(L₁) = a₀ + 1

branch 2:
μ(L₁) = a₀ + 2
```

이면 merge 후:

```text
μ(L₁) = TOP
```

이다.

이 경우 exit memory summary에서도:

```text
μ_normal_exit(L₁) = TOP
```

으로 유지한다.

Exit constraint가 TOP에 의존하면 SSA-origin 복원을 시도한다. 끝내 실패하면
Section 8에 따라 그 conjunction의 종료 보장 인증을 생략하고 Partial로 표시한다.
Memory value의 TOP과 종료 보장 조건의 실패를 같은 true 약화 규칙으로 처리하지 않는다.


## 11. 향후 CFG 정밀도 확장

σ/μ는 branch/loop merge 시 서로 다른 symbolic value를 TOP으로 추상화한다.
ρ는 §2.6의 interval join/widening을 사용한다. 두 component의 수렴 규칙을 혼동하지 않는다.

향후 다음 방식으로 확장할 수 있다.

```text
1. branch별 symbolic state 유지
2. symbolic value의 finite disjunction
3. path-sensitive symbolic state
4. path condition과 σ/μ 사이의 correlation 보존
5. loop에 대한 보다 정밀한 symbolic invariant 추론
```

현재 범위에는 Section 3.2의 nullity Boolean origin과 §2.5.1의 acyclic 분기별
대입을 종료 조건으로 복원하는 기능이 포함된다. 일반적인 path partitioning과
임의의 disjunctive σ/μ 값 상태는 후속 확장이다.


## 12. 추가 불변조건

```text
Invariant 20.
μ_normal_exit과 μ_exception_exit에는 caller-visible memory location만
포함한다.

Invariant 21.
callee 내부 Allocate로 생성된 local location은 exit의 전체 μ에는 존재할
수 있지만 caller에 전달되는 exit memory summary에서는 제거한다.

Invariant 22.
exit memory filtering은 AR 이름의 '$' 문자 여부가 아니라 location의
origin이 현재 callee의 Allocate인지 여부를 기준으로 수행한다.

Invariant 23.
CFG merge에서 동일한 symbolic value는 그대로 유지한다.

Invariant 24.
Boolean 생성 정규화 이후 CFG merge에서 서로 다른 symbolic value는 TOP으로 병합한다.

Invariant 25.
LLVM PHI에 의해 predecessor별로 동일한 AR variable에 서로 다른 값이
대입된 경우 CFG merge 규칙으로 처리한다.

Invariant 26.
loop 보조 state는 IKOS의 기존 WTO fixpoint lifecycle과 함께 갱신한다.
σ/μ의 flat merge와 ρ의 interval join/widening을 각각 적용하고 전체 state의 수렴을 확인한다.

Invariant 27.
σ/μ에는 별도의 widening을 적용하지 않으며 ρ에는 interval widening을 적용한다.

Invariant 28.
σ/μ에는 narrowing을 적용하지 않으며 ρ에는 증명된 interval narrowing만 허용한다.

Invariant 29.
exit constraint는 affine σ 또는 조건 전용 SSA origin에서 복원한다.
실패하면 더 강한 영역을 증명하거나 해당 conjunction을 인증하지 않고 Partial로 표시한다.

Invariant 30.
인증된 영역이 없으면 false/Partial 또는 Unavailable이다.
false/Complete만 flow 불가능을 뜻하고 true는 모든 입력의 종료 보장을 뜻한다.

Invariant 31.
여러 normal exit의 parameter-based condition은 disjunction으로 결합한다.

Invariant 32.
여러 exception exit의 parameter-based condition은 disjunction으로 결합한다.

Invariant 33.
formal symbolic location은 source-level parameter 이름에 의존하지 않고
AR parameter identity 또는 parameter index를 기준으로 표현한다.

Invariant 34.
PointerShift의 affine byte offset은 Loc(B, δ)에 누적하며, 서로 다른 δ의
location은 서로 다른 memory location으로 취급한다.

Invariant 35.
도달 가능한 state에서 missing entry는 TOP이다. σ/μ는 MergeVal,
ρ는 typed interval join을 사용한다. 한 incoming에 없는 ρ key를 다른 경로에서 가져오지 않는다.

Invariant 36.
ρ는 같은 typed operand identity의 표현식에만 적용하며 재정의 시 의존 key를 무효화한다.

Invariant 37.
ρ에서 얻은 범위는 checker가 읽는 A에 반영하고, C_X의 충분조건으로 재명명하지 않는다.

Invariant 38.
29번의 comparison은 분기 guard와 대입식을 치환한 DNF로 복원하여 마지막 검사를 보존한다.

Invariant 39.
30번의 variable-variable UMul은 typed 조건으로 보존하며 affine σ의 제한으로 버리지 않는다.
```


## 13. 본 문서의 범위

본 문서는 callee 내부의 σ/μ/ρ 및 numerical state와의 정제를 계산하고 exit summary를 생성하기 위한 transfer/CFG 규칙을 정의한다.

최종 산출물은 다음과 같다.

```text
NormalSummary(g) =
    (C_normal(g), Coverage_normal(g), μ_normal_exit,g)

ExceptionSummary(g) =
    (C_exception(g), Coverage_exception(g), μ_exception_exit,g)
```

이 summary를 caller call site에서 argument에 맞게 치환하고 normal-flow pruning 및 caller memory update에 적용하는 과정은 별도의 pruning/application 명세에서 정의한다.
