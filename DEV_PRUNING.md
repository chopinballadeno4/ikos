# Caller Summary Pruning 명세

## 1. 문서 목적과 범위

이 문서는 callee에서 생성한 Parameter-Origin Symbolic Exit Summary를 caller의
`Call` 또는 `Invoke`에 적용하는 방법을 정의한다. 기존 IKOS call semantics가
계산한 abstract state를 기준으로 callee의 정상 종료 조건, 예외 종료 조건 및
caller-visible memory effect를 재사용하여 각 continuation을 정제하는 것이
목적이다.

Summary는 기존 IKOS 분석을 대체하지 않는다. Callee resolution, 함수 본문 분석,
return 처리, exception 처리 및 memory 처리는 기존 IKOS semantics를 먼저
수행하며, caller summary pruning은 그 결과에 추가되는 sound한 refinement로만
동작한다.

현재 범위는 다음과 같다.

- 단일 callee로 확정되는 direct 또는 resolved indirect call
- Normal flow와 exception flow의 독립적인 pruning
- Affine 값 관계와 별개인 typed 종료 보장 조건의 caller 문맥 치환
- Partial/Complete coverage에 따른 적용 제한과 미확인 영역 보존
- Pointer/reference parameter를 통한 caller symbolic memory effect
- `Call`과 `Invoke`의 서로 다른 exception routing
- 정보가 부족하거나 표현할 수 없을 때의 conservative fallback

Global variable summary, 정밀한 aggregate/vector relation, return-value summary,
복수 callee summary join 및 recursive summary는 현재 범위에 포함하지 않는다.

---

## 2. 기호와 용어

이 절에서 문서 전체에 사용되는 기호를 정의한다. 이후 절에서는 같은 기호의
의미를 반복해서 설명하지 않는다.

### 2.1 프로그램 요소

| 기호 | 의미 |
| --- | --- |
| `f` | Call site를 포함하는 caller 함수 |
| `g` | Call site에서 호출되는 callee 함수 |
| `c` | Caller `f` 내부의 `Call` 또는 `Invoke` call site |
| `p_i` | Callee `g`의 `i`번째 formal parameter |
| `arg_i` | Call site `c`의 `i`번째 actual argument |
| `X` | 종료 flow의 종류 |
| `N` | Normal-return flow |
| `E` | Caller로 전파되는 exception flow |

`X`는 문맥에 따라 `N` 또는 `E` 중 하나를 나타낸다.

### 2.2 기존 IKOS abstract state

| 기호 | 의미 |
| --- | --- |
| `A` | 기존 IKOS abstract state |
| `A_c^pre` | Call instruction을 실행하기 직전의 abstract state |
| `A_{c,X}^base` | 기존 IKOS call semantics가 생성한 flow `X`의 state |
| `A_{c,X}^pruned` | 종료 조건을 적용한 뒤의 flow `X` state |
| `A_{c,X}^post` | Memory summary까지 적용한 최종 flow `X` state |
| `⊤` | 해당 domain에서 아무 제약도 없는 상태 또는 값 |
| `⊥` | 도달 불가능한 abstract state |
| `γ(A)` | Abstract state `A`가 나타내는 concrete state 집합 |
| `⊑` | Abstract-domain precision order |
| `⊔` | Abstract-domain join |

이 문서에서 `A_`가 붙은 기호는 기존 IKOS abstract analysis에서 사용하는 상태나
그 상태에 적용할 표현을 의미한다. 특히 위 첨자 `pre`, `base`, `pruned`, `post`는
call site 처리 과정의 서로 다른 시점을 나타낸다.

### 2.3 보조 symbolic state

보조 symbolic state는 다음과 같이 구성한다.

```text
Σ = (σ, μ, ρ)
```

| 기호 | 의미 |
| --- | --- |
| `Σ` | σ/μ의 parameter-origin 값 관계와 ρ의 현재 표현식 범위를 보존하는 보조 state |
| `ρ` | Typed ExprKey에 현재 flow의 sound machine-integer interval을 연결하는 map |
| `σ` | Scalar/pointer/Boolean value를 parameter-origin symbolic value에 연결하는 환경 |
| `μ` | Caller-visible location을 symbolic value에 연결하는 memory |
| `Σ_c^pre` | Call 직전의 symbolic state snapshot |
| `Σ_{c,X}^base` | 기존 call effect를 보수적으로 적용한 flow `X`의 symbolic state |
| `Σ_{c,X}^post` | Memory summary까지 적용한 flow `X`의 symbolic state |
| `Expr` | σ/μ에서 지원되는 parameter-origin affine 값 관계 |
| `CondExpr` | 종료 조건 전용 typed expression; %·/·USub·변수 곱 및 분기별 origin 복원 포함 |
| `PtrExpr` | Null 또는 PtrValue(Loc); pointer 값이며 target의 유효성을 가정하지 않음 |
| `BoolExpr` | 증명된 nullity predicate의 정확한 0/1 encoding |
| `Loc` | 지원되는 symbolic memory location |
| `TOP` | 정확한 symbolic relation을 표현할 수 없음 |

`ρ : ExprKey ⇀ MachineIntInterval`은 현재 flow에서 표현식 자체의 가능한 값을
포함하는 interval을 저장한다. 공통 정의와 값 identity는 DEV_SUMMARY.md §2.7~2.10을
따른다. `σ/μ`에 조건을 저장하지 않으며 별도의 일반 Boolean path-condition
component는 두지 않는다. 적용이 허용된 조건에서 얻은 표현식 범위는 ρ에 저장한다.
ρ의 interval은 종료 보장 충분조건 C_X로 재해석하지 않는다.

### 2.4 Callee entry atom과 summary

| 기호 | 의미 |
| --- | --- |
| `p_i^0` | Callee 진입 시점의 `i`번째 scalar parameter 값 |
| `P_i^0` | Entry pointer parameter 값, 기존 Loc 표현의 PtrValue(L_i) view |
| `L_i` | Pointer/reference parameter를 통한 entry memory location |
| `L + δ` | `L`로부터 정확한 offset `δ`에 있는 derived location |
| `v^0(L)` | Callee 진입 시점 location `L`의 scalar content |
| `ℓ^0(L)` | Callee 진입 시점 location `L`의 pointer-valued content |
| `C_X(g)` | Callee g가 flow X로 종료함을 보장하는 입력 충분조건 |
| `Coverage_X(g)` | Partial 또는 Complete; 해당 flow 입력의 포괄성 증명 |
| `GuaranteedExitInputs_X(g)` | 해당 입력의 모든 허용 실행이 정의된 동작으로 유한 시간 내 X 종료 |
| `PossibleExitInputs_X(g)` | X로 종료하는 실행이 존재하는 입력 |
| `μ_X(g)` | Flow `X` 종료 시의 caller-visible memory summary |

Flow별 FunctionSummary는 다음 형식이다.

```text
NormalSummary(g)    = (C_N(g), Coverage_N(g), μ_N(g))
ExceptionSummary(g) = (C_E(g), Coverage_E(g), μ_E(g))
```

각 flow summary의 가용 상태는 다음 중 하나이다.

```text
Summary_X(g) = Known(C_X(g), Coverage_X(g), μ_X(g))
             | Unavailable
```

`Known(false, Partial, ∅)`은 보장 영역이 없다는 뜻이며 flow 불가능의 증거가 아니다.
`Known(false, Complete, ∅)`만 해당 flow가 불가능함을 뜻한다.
`Known(true, Complete, μ_X(g))`는 모든 허용 입력에서 해당 종료를 보장한다는 뜻이다.
`Unavailable`은 사용할 수 있는 summary가 없다는 뜻이다. 이 세 종류의 미확인,
보장 및 불가능을 구분한다. 구체적인 계약은 DEV_SUMMARY.md §1.1·3을 따른다.

### 2.5 Caller 문맥 mapping과 치환 결과

| 기호 | 의미 |
| --- | --- |
| `κ_c` | Callee entry atom을 현재 caller의 IKOS 표현으로 변환하는 mapping |
| `θ_c` | Callee entry atom을 caller의 parameter-origin 표현으로 변환하는 mapping |
| `C_{c,X}^A` | κ_c로 정확히 치환한 보장 조건; Complete일 때만 전체 flow 필터로 사용 |
| `C_{c,X}^Σ` | θ_c로 치환한 보장 영역; 환원 실패 시 영역 축소 및 coverage 재평가 |
| `Effects_{c,X}^A` | IKOS memory refinement에 사용할 caller 문맥의 memory effect |
| `Effects_{c,X}^Σ` | Symbolic memory에 사용할 caller 문맥의 memory effect |
| `K_c(p_i^0)` | Call-entry에서 i번째 actual argument 값을 나타내는 typed ExprKey |
| `ρ_{c,X}^base/pruned/post` | 각 단계의 flow별 표현식 범위 상태; Σ의 ρ component |

`κ_c`와 `θ_c`는 모두 변경되지 않는 동일한 call-entry snapshot을 기준으로
계산한다.

### 2.6 주요 연산

| 연산 | 의미 |
| --- | --- |
| `ResolveCallees(c, A)` | Call site에서 가능한 callee 집합 계산 |
| `Normalize(v, T)` | 값 `v`를 타입 `T`의 bitwidth와 signedness에 맞게 정규화 |
| `Eval(v, Σ)` | Caller value를 parameter-origin expression으로 평가 |
| `ResolveLoc(v, A)` | Pointer/reference 값의 정확한 caller location 계산 |
| `EvalLoc(v, Σ)` | Pointer/reference 값을 symbolic location으로 평가 |
| `SubstCondition(C, m)` | Mapping `m`을 이용한 조건 치환 |
| `Assume(A, a)` | Atomic constraint `a`로 abstract state `A` 정제 |
| `PruneFlow(A, C)` | Complete 조건을 이용한 flow pruning; Partial에는 호출하지 않음 |
| `EntailsEntry((A,ρ), C)` | Call-entry의 실제 입력 전체가 C를 만족한다는 증명; 유효한 ρ도 사용 가능 |
| `RefineMemory(A, E)` | Memory effect `E`로 기존 IKOS memory state 정제 |
| `ApplySymbolicMemory(Σ, E)` | Symbolic memory effect `E` 적용 |

### 2.7 적용 상태

```text
ApplicationStatus_{c,X}
    ∈ {Applied, PartiallyApplied, Pruned, Skipped(reason)}
```

| 상태 | 의미 |
| --- | --- |
| `Applied` | Flow summary가 정상적으로 적용됨 |
| `PartiallyApplied` | 유효성/coverage를 확인한 영역에서 일부 보장 정보만 적용됨 |
| `Pruned` | 종료 조건에 의해 flow가 `⊥`이 됨 |
| `Skipped(reason)` | Summary 전체를 적용하지 않고 기존 semantics를 유지함 |

이 상태는 diagnostic 정보이며 분석 의미를 변경하지 않는다.

---

## 3. 단계 1 — Pruning의 의미

### 3.1 기존 분석에 대한 refinement

Caller summary pruning은 기존 IKOS call semantics가 계산한 flow별 base state에
Complete로 인증된 callee 종료 조건을 추가하는 연산이다. 아래 pruning 식은 Complete에만 적용한다.

```text
A_{c,N}^pruned = PruneFlow(A_{c,N}^base, C_{c,N}^A)
A_{c,E}^pruned = PruneFlow(A_{c,E}^base, C_{c,E}^A)
```

Pruning 결과는 기존 state보다 같거나 더 정밀해야 한다.

```text
A_{c,X}^pruned ⊑ A_{c,X}^base
```

Summary는 기존 abstract state를 새 상태로 교체하지 않는다. 기존 IKOS가 계산한
numerical constraint, pointer 정보, exception 정보 및 call effect를 보존한 상태에서
summary가 보장하는 조건만 추가한다.

### 3.2 Flow별 적용

Normal condition은 normal-return flow에만 적용한다.

```text
C_N(g) → A_{c,N}^base
```

Exception condition은 exception flow에만 적용한다.

```text
C_E(g) → A_{c,E}^base
```

한 flow의 조건을 다른 flow에 적용하거나 두 조건을 합쳐 하나의 continuation에
적용하지 않는다.

### 3.3 Program exit

프로그램 자체를 종료하는 실행은 caller의 normal continuation이나 exception
continuation으로 전달하지 않는다. 따라서 program-exit condition은 caller의
후속 state를 생성하지 않는다.

Normal flow와 exception flow가 모두 불가능하고 program exit만 가능하면 call site
이후에는 successor state가 없다.

### 3.4 종료 보장과 caller soundness

```text
[[P_memory(g) ∧ C_X(g)]] ⊆ GuaranteedExitInputs_X(g) ⊆ PossibleExitInputs_X(g)
Coverage_X(g)=Complete ⇒ (PossibleExitInputs_X(g) ∩ [[P_memory(g)]]) ⊆ [[C_X(g)]]
Reach_{c,X} ⊆ γ(A_{c,X}^post)
```

첫 번째는 summary의 충분조건 계약이고 마지막은 분석기 전체의 안전성이다.
P_memory가 없으면 true이며 기존 계약과 같다. 전제가 있는 summary는 call 직전의
normal state 전체가 치환된 P_memory를 만족함을 먼저 증명해야 한다. Post state의
NonNull이나 store 이후의 값으로 entry 전제를 소급해서 증명하지 않는다. 증명
실패 시 두 flow의 C_X와 μ_X를 모두 적용하지 않고 기존 IKOS 결과를 유지한다.
검증은 sequential/concurrent inlining 모두 동일하게 적용한다.

현재 native 적용에서 Writable을 해소하는 범위는 initialized non-null pointer의
단일 points-to가 살아 있는 stack scalar이고 offset 0, 정확한 pointee type,
할당 크기와 정렬이 모두 확인되는 경우다. Null/unknown/multiple targets, 수명
미확인, 크기 부족, 타입/정렬 불일치 및 현재 미지원 heap/global은 전제 검증 실패다.
이는 오류를 새로 단정하거나 flow를 제거하는 이유가 아니다. 기존 checker가 원래
오류를 계속 검사한다. Symbolic fixpoint의 일반 memory 적용은 이런 native 증명이
없으므로 조건부 summary를 적용하지 않는다. 별도 exit proof에서 명시적으로
전제를 해소/전파한 영역에서만 callee의 조건부 memory 관계를 재사용한다.
실제 정상 종료 범위가 `[1,1000]`일 때 `[1,500]`은 Partial 보장 조건이 될 수
있지만, 이 조건으로 모든 normal base state를 잘라 `[501,1000]`의 실제 실행을
없애서는 안 된다. `[1,1500]`은 종료 보장 조건으로 저장할 수 없다.

Complete일 때 C_X는 해당 flow의 필요조건이기도 하므로 전역 flow pruning에
사용할 수 있다. Partial일 때 C_X 밖은 미확인이며 기존 IKOS base state를 유지한다.
입력 전체가 C_X를 만족한다는 독립적인 증명 `EntailsEntry((A_c^pre,ρ_c^pre),C_X)`이 있으면
그 call context에서만 보장된 memory 관계를 적용할 수 있다. 단순히 A의 interval이
C_X와 겹치거나 abstract Assume 결과가 non-bottom인 것은 포함 증명이 아니다.

`Partial=false`, origin 복원 실패 또는 domain 미지원 때문에 실제 flow를 제거하거나
안전 판정을 내리지 않는다. 보장되지 않은 입력에서 기존 오류 검사를 계속 수행한다.

---

## 4. 단계 2 — Summary 적용 가능 여부

### 4.1 Callee resolution

```text
Targets(c) = ResolveCallees(c, A_c^pre)
```

현재 명세에서 summary 적용은 다음 조건을 모두 만족할 때만 가능하다.

```text
Applicable(c, g, A_c^pre) ⇔
    Targets(c) = {g}
    ∧ BodyAvailable(g)
    ∧ TypeCompatible(c, g)
    ∧ ¬RecursiveApplication(c, g)
```

가능한 callee가 없거나 여러 개이면 summary를 적용하지 않는다. External function,
본문을 사용할 수 없는 함수, 호출 타입이 호환되지 않는 함수 및 현재 분석 chain의
recursive call에도 summary를 적용하지 않는다.

이 경우 기존 IKOS call semantics가 계산한 보수적인 결과를 그대로 사용한다.

### 4.2 Flow summary의 독립적인 가용성

Normal summary와 exception summary의 가용성은 서로 독립적으로 판단한다.

```text
Summary_N(g) = Known(C_N(g), Coverage_N(g), μ_N(g)) | Unavailable
Summary_E(g) = Known(C_E(g), Coverage_E(g), μ_E(g)) | Unavailable
```

한 flow의 summary가 `Unavailable`이어도 다른 flow의 `Known` summary는 적용할 수
있다.

### 4.3 Summary 상태별 처리

```text
Known(false, Complete, ∅) ⇒ 해당 flow 제거
Known(false, Partial, ∅)  ⇒ base state 유지
Unavailable              ⇒ base state 유지
```

Complete 조건은 정확한 caller 치환 후 해당 flow pruning에 사용할 수 있다.
Partial 조건은 전체 base state의 필터로 사용하지 않는다. Call-entry 전체가 그
보장 영역에 포함된다는 증명이 있으면 그 context에서만 보장 관계를 적용한다.
증명이 없으면 해당 flow의 memory를 포함한 기존 base 결과를 유지한다.

`Known(true, Complete, μ)`의 true는 전체 입력의 종료 보장이다. Unknown을 뜻하는
placeholder로 만들지 않는다. Normal과 exception의 coverage는 서로 독립적이다.

---

## 5. 단계 3 — Parameter–Argument Mapping

### 5.1 두 종류의 mapping

```text
κ_c : CalleeAtom → CallerARValue | CallerSnapshot | CallerCondExpr | unrepresentable
θ_c : CalleeAtom → Expr | CondExpr | Loc | Null | BoolExpr | TOP
```

`κ_c`는 현재 call site의 IKOS state를 즉시 pruning하거나 refinement하기 위한
표현을 생성한다. `θ_c`는 callee summary를 caller의 parameter-origin 표현으로
합성하기 위한 표현을 생성한다.
CondExpr는 종료 조건 치환에서만 사용한다. Non-affine CondExpr를 σ/μ의 affine
SymbolicValue로 임의 저장하지 않으며 memory relation의 기존 적용 제한은 유지한다.

모든 mapping은 다음 snapshot을 기준으로 계산한다.

```text
(A_c^pre, Σ_c^pre)
```

`K_c`도 같은 시점에 생성한다. Actual argument가 임시 t1이라도 그 정의가
`UAdd_w(a,b)`임을 확인하면 `K_c(p_i^0)=UAdd_w(ValueId(a),ValueId(b))`로 보존한다.
σ(t1)가 TOP이거나 actual이 caller parameter로 환원되지 않아도 이 local key는
유효할 수 있다. Call 이후 liveness로 t1이 사라진 다음 key를 복원하려고 하지 않는다.

Memory summary의 앞선 update가 뒤의 target 또는 value 계산에 영향을 주어서는 안
된다.

### 5.2 Scalar parameter

```text
κ_c(p_i^0) = Normalize(arg_i, Type(p_i))
θ_c(p_i^0) = Eval(arg_i, Σ_c^pre)
```

위 Eval이 정확한 값을 제공하지 못하면 종료 조건 치환에서는 검증된 SSA origin을
추가 조회하여 parameter-origin CondExpr를 복원한다. 그마저 불가능할 때만 TOP으로
처리한다. 여러 reaching definition은 guard와 값을 대응시킨 DNF로 치환한다.
이 조건 전용 복원은 σ/μ memory-value 치환을 비선형으로 확장하는 요구가 아니다.

Actual argument의 bitwidth, signedness 및 cast 의미를 callee parameter 타입에 맞게
보존할 수 있어야 한다. 안전한 정규화가 불가능하면 각각 `unrepresentable` 또는
`TOP`으로 처리한다.

### 5.3 Pointer/reference parameter

포인터 값과 메모리 위치의 mapping을 구분한다.

```text
κ_c(P_i^0) = actual pointer value arg_i
θ_c(P_i^0) = EvalPointer(arg_i, Σ_c^pre)
```

Pointer nullity condition 적용에는 정확한 pointee location이나 single-target
points-to 정보를 요구하지 않는다. κ로 actual pointer에 적용할 수 있으면 θ가 TOP인
경우에도 현재 caller state를 정제할 수 있다. 따라서 get_ptr() 반환값의 symbolic
origin이 TOP이어도 actual pointer 변수에 NonNull 조건을 적용할 수 있다.

아래 location mapping의 정확성 요건은 memory 접근/effect에 적용한다.

```text
κ_c(L_i) = ResolveLoc(arg_i, A_c^pre)
θ_c(L_i) = EvalLoc(arg_i, Σ_c^pre)
```

정확한 base location을 확인할 수 있을 때 derived location의 offset을 그대로
보존한다.

```text
κ_c(L_i + δ) = κ_c(L_i) + NormalizeOffset(δ)
θ_c(L_i + δ) = θ_c(L_i) + NormalizeOffset(δ)
```

Offset의 단위, pointee type 및 memory range가 호환되지 않으면 정확한 location으로
취급하지 않는다.

### 5.4 Entry-memory atom

```text
κ_c(v^0(L)) = ReadSnapshot(A_c^pre, κ_c(L))
θ_c(v^0(L)) = ReadSymbolicSnapshot(Σ_c^pre, θ_c(L))
```

```text
κ_c(ℓ^0(L)) = ReadPointerSnapshot(A_c^pre, κ_c(L))
θ_c(ℓ^0(L)) = ReadSymbolicPointerSnapshot(Σ_c^pre, θ_c(L))
```

Entry-memory atom은 call이 실행된 뒤의 memory가 아니라 call 직전 snapshot의
content를 나타낸다.

필요한 entry-memory relation이 기존 `Σ_c^pre`에 명시되어 있지 않다면 다음 조건을
모두 만족하는 경우에만 caller parameter 기준의 relation을 lazy하게 생성할 수
있다.

- 정확한 caller parameter-relative location이다.
- Call 이전에 해당 relation을 변경하는 write가 없다.
- 해당 relation이 invalidation되지 않았다.
- 지원되는 scalar 또는 pointer type이다.
- Uninitialized, unknown 또는 multiple-target location이 아니다.

그 외의 경우에는 임의의 entry relation을 생성하지 않는다.

### 5.5 Nested pointer

Pointer-valued entry-memory atom은 같은 규칙을 재귀적으로 적용한다.

```text
θ_c(ℓ^0(L_i)) = L_j 또는 TOP
θ_c(v^0(ℓ^0(L_i) + δ)) = ReadSymbolicSnapshot(Σ_c^pre, L_j + δ)
```

각 dereference 단계에서 정확한 single target과 유효한 memory range가 확인되어야
한다. 어느 한 단계라도 불확실하면 이후 relation을 `TOP`으로 처리한다.

### 5.6 Mapping 실패

Mapping 실패는 flow가 불가능하다는 의미가 아니다.

```text
κ_c(atom) = unrepresentable
θ_c(atom) = TOP
```

실패한 atom은 이후 condition 또는 memory 적용 단계의 conservative fallback
규칙에 따라 처리한다.

---

## 6. 단계 4 — 종료 조건 치환

### 6.1 Caller 조건 생성

```text
C_{c,X}^A = SubstCondition(C_X(g), κ_c)
C_{c,X}^Σ = SubstCondition(C_X(g), θ_c)
```

`C_{c,X}^A`는 Complete일 때 현재 IKOS abstract state의 pruning에 사용하고,
Partial일 때에는 call-entry의 보장 영역 포함 증명에만 사용한다.
`C_{c,X}^Σ`는 인증된 영역의 caller exit-condition을 구성할 때 사용한다.
두 결과 모두 coverage와 치환 성공 여부를 별도로 유지한다.

### 6.2 조건 형식

종료 조건은 DNF로 정규화한다.

```text
C_X(g) = D_1 ∨ ... ∨ D_m
D_i    = c_i1 ∧ ... ∧ c_in
```

AffineExpr와 typed CondExpr 내부의 모든 callee entry atom을 재귀적으로 치환하며, 다음
정보를 보존한다.

- Integer와 pointer type
- Bitwidth
- Signedness
- Comparison predicate
- Exact location과 offset
- SRem/URem/SDiv/UDiv의 연산 종류와 정의 가능 전제
- USub/UMul의 wrapping 의미 및 양쪽 operand; 상수 operand/no-wrap을 요구하지 않음
- 29번의 분기 guard와 대입식을 대응시킨 DNF 및 마지막 검사
- 지원하는 ITE의 guard 및 양쪽 operand

### 6.3 표현 가능한 atom

DEV_SUMMARY.md §2.6의 CondExpr 비교와 IsNull(P_i^0), NonNull(P_i^0)를 지원한다.
예를 들어 `SRem_w(p_i^0,2)==0`은 내부 parameter까지 actual argument로 재귀 치환한다.
29번의 `URem_w(i,2)`·`USub_w(i,1)`과 30번의 `UMul_32(s,i)`도 같은 방식으로
치환한다. 두 operand가 변수라는 이유로 곱셈 조건을 생략하지 않는다. 29번의
분기별 DNF와 30번의 마지막 곱셈 비교는 저장·출력 및 상위 summary 합성에서도
보존한다(DEV_SUMMARY.md §5.3·5.4).
치환으로 생긴 wrapping 연산을 수학적 affine 덧셈으로 바꾸지 않는다. 조건을 정확히
치환할 수 있다는 사실과 numerical domain에서 정제할 수 있다는 사실은 구분한다. Null constant는
별도로 보존한다. θ 치환으로 pointer origin이 바뀌면 해당 caller parameter identity를
사용하고, κ 치환은 actual pointer value를 직접 사용한다. Offset 0의 origin만
지원하며 derived pointer 관계를 base pointer의 nullity로 대체하지 않는다.

Boolean actual argument는 증명된 exact origin을 이용한다.

```text
θ_c(cond_i^0) = BooleanValue(G)
cond_i^0 == 1 또는 != 0 → G
cond_i^0 == 0 또는 != 1 → ¬G
```

κ의 Boolean-origin 조회도 전체 definition과 모든 entry-to-use 경로의 definition 통과를
증명해야 한다(DEV_TRANSFER_FUNCTION.md §3.2). 한 block에서 true assignment와
pointer comparison을 발견한 것만으로 관계를 가정하지 않는다. Unknown은 부정해도
unknown이다. 정규화 후 미지원 atom은 §6.4의 복원 실패 규칙을 따른다.

Atomic constraint의 모든 operand가 안전하게 치환되면 같은 predicate를 유지한
caller constraint를 생성한다.

```text
SubstCondition(e_1 op e_2, m)
    = SubstExpr(e_1, m) op SubstExpr(e_2, m)
```

### 6.4 표현 불가능한 atom

Summary를 다른 parameter-origin summary로 합성할 때, 치환 불가능한 atom을 true로
바꾸어 보장 영역을 넓히지 않는다. 정확한 origin 복원을 우선하고, 더 강한 대체
영역을 증명하지 못하면 그 atom을 포함한 전체 conjunction의 보장 기여를 생략한다.
결과 coverage는 기본 Partial이며 상위 함수의 coverage를 다시 증명한다.

```text
Failed θ substitution of required atom
    ⇒ omit uncertified conjunction; coverage=Partial
    ⇏ actual path impossible
```

즉시 IKOS에 적용하는 κ 치환이 정확하지 않으면 기본적으로 그 flow의 summary
pruning을 생략하고 base state를 유지한다. κ 실패를 true 보장 조건으로 저장하지
않는다. 한쪽 mapping 실패는 다른 mapping의 유효성을 자동으로 부정하지 않지만,
각 사용처는 자신의 계약과 coverage를 독립적으로 확인해야 한다.

### 6.5 Boolean 단순화

치환 후 다음 규칙으로 조건을 단순화한다.

```text
D ∧ true  = D
D ∧ false = false
empty conjunction = true
```

```text
C ∨ false = C
C ∨ true  = true
empty disjunction = false
```

Disjunction 구조는 유지한다. 여기의 논리 disjunction `∨`는 abstract-domain join
`⊔`과 구분한다.

원래 조건이 true 또는 false이면 논리값은 그대로 유지하되 coverage metadata를
함께 전달한다. false/Partial을 false/Complete로 바꾸지 않는다. 조건 제거는 동치가
증명되거나 더 강한 보장 영역을 별도로 검증했을 때만 허용한다.
29/30번의 원래 검사 보존 요구는 이 일반 규칙보다 우선한다. 완전히 동일한 atom의
중복 제거와 모순 경로 제거는 허용하지만 parity·USub·UMul 검사를 함의 관계로
삭제하지 않는다. Numerical domain이 적용하지 못해 no-op으로 처리하더라도 원래
symbolic 조건을 다시 쓰거나 삭제하지 않는다.

---

## 7. 단계 5 — Flow별 Condition 적용 및 Pruning

§7.1~7.3의 전역 pruning은 Complete summary의 정확히 치환된 조건에만 적용한다.
Abstract domain의 Assume/join은 여전히 가능한 실제 상태를 모두 포함해야 한다.
C_X를 abstract state로 근사한 결과를 새로운 종료 보장 C_X로 재사용하지 않는다.
아래 A만을 사용한 수식은 numerical projection의 표기다. 실제 처리는 §7.6의
`(A,ρ)` pair와 σ/μ를 함께 전달하며, ρ를 분기 밖의 공통 mutable cache로 두지 않는다.

### 7.1 Conjunction 적용

```text
Assume(A, NonNull(ptr)) = ptr nullity를 non-null과 교차 정제
Assume(A, IsNull(ptr))  = ptr nullity를 null과 교차 정제
```

Contradiction이면 해당 branch는 bottom이다. Pointer target 해석이나 memory access를
수행하지 않는다. 이 refinement는 integer Assume와 동일하게 conjunction 내부에서 수행한다.

```text
PruneFlow(A, c_1 ∧ ... ∧ c_n)
    = Assume(...Assume(Assume(A, c_1), c_2)..., c_n)
```

```text
PruneFlow(A, true)  = A
PruneFlow(A, false) = ⊥
PruneFlow(⊥, C)     = ⊥
```

각 atomic constraint는 같은 conjunction state에 순차적으로 적용한다. 중간 결과가
`⊥`이면 나머지 atom을 처리하지 않는다.

### 7.2 Disjunction 적용

```text
PruneFlow(A, D_1 ∨ ... ∨ D_m)
    = ⊔ { PruneFlow(A, D_i) | PruneFlow(A, D_i) ≠ ⊥ }
```

Integer, pointer nullity 및 Boolean-origin refinement는 모두 각 disjunct의 state
안에서 수행한 후 join한다. 일부 disjunct에만 있는 non-null 조건을 join 뒤의 전체
state에 강제하지 않는다. 이 규칙은 normal/exception 두 flow에 동일하게 적용한다.

모든 disjunct는 동일한 입력 state `A`에서 독립적으로 시작한다. 하나의 state에
disjunct를 순차 적용하면 conjunction 의미가 되므로 허용하지 않는다.

살아남은 disjunct가 없으면 결과는 `⊥`이다. 여러 disjunct가 살아남으면 기존 IKOS
abstract domain의 join으로 결합한다. Join 과정에서 disjunctive precision이 일부
손실될 수 있지만 soundness는 유지한다.

### 7.3 Flow별 pruning

```text
A_{c,N}^pruned = PruneFlow(A_{c,N}^base, C_{c,N}^A)
A_{c,E}^pruned = PruneFlow(A_{c,E}^base, C_{c,E}^A)
```

Summary를 적용할 수 없거나 coverage가 Partial인 flow는 base state를 유지한다.
Partial 조건 밖을 삭제하지 않으며, 가능한 경우의 조건부 효과는 §7.5를 따른다.

IKOS domain이 치환된 atom을 표현하지 못하면 그 atom으로 state를 bottom으로 만들지 않는다. 다만 ρ가 그 typed 식의 범위를
표현할 수 있으면 §7.6으로 정제하고, 둘 다 지원하지 못하면 현재 state를 보존한다. 반면 sound하게 표현된 constraint와 기존 state의
교집합이 실제로 `⊥`이면 유효한 pruning 결과로 인정한다.

### 7.4 Symbolic condition의 사용

`C_{c,X}^Σ`는 σ/μ의 값 관계에 직접 Assume하지 않는다. Caller의 충분조건을
합성하는 입력으로 전달하며 별도의 종료 보장 검증을 거친다. 현재 실제 flow에서
성립함이 확인된 수치 조건은 §7.6에 따라 ρ를 정제할 수 있다. Parameter-origin
조건 합성과 현재 flow의 범위 정제는 서로의 증명을 자동으로 대체하지 않는다.

Caller 충분조건 합성은 해당 영역에서의 call 종료 보장에만 사용한다. 미확인
call 입력을 전체 caller 분석에서 삭제하는 데 사용하지 않는다. Numerical domain이
표현하지 못한 `%`/NE atom도 정확히 치환된 symbolic 조건에는 그대로 남긴다.
Memory summary는 아래의 유효 영역 검사도 통과해야 한다.

### 7.5 Partial 조건의 적용 제한

```text
Coverage_X=Partial AND NOT Proven(EntailsEntry((A_c^pre,ρ_c^pre), C_{c,X}^A))
    ⇒ A_{c,X}^post=A_{c,X}^base; Σ_{c,X}^post=Σ_{c,X}^base
```

부분 summary의 밖을 별도로 분석하는 guarded partition 확장을 도입할 수 있지만,
정확한 분할과 complement, 각 영역의 call effect, 결과 join을 함께 정의해야 한다.
현재 기본 규칙은 분할을 요구하지 않고 위처럼 적용을 생략한다. 부분 영역에서만
성립하는 효과를 abstract interval hull로 확대한 영역에 적용하지 않는다.

### 7.6 표현식 범위 ρ에 대한 summary 적용

기존 IKOS domain이 a와 b 각각의 범위만 표현하더라도 `a+b`를 하나의 typed 식
key로 삼아 그 범위를 보존한다. Call-entry의 `K_c`를 사용하여 치환한 식의 값
identity를 유지한다. Basic block, normal/exception 및 DNF branch마다 독립적인
ρ를 사용한다. 다음 조건 중 하나일 때만 summary 조건을 현재 flow의 사실로
사용한다.

```text
Coverage_X=Complete
OR Proven(EntailsEntry((A_c^pre,ρ_c^pre), C_{c,X}^A))
```

Partial을 만났다는 이유만으로 그 조건을 ρ에 넣지 않는다. Key의 입력 값이 call
effect 뒤에도 같은 값을 의미하는지 확인하고, 변하거나 불명확해진 binding에
의존하는 fact는 먼저 무효화한다. Frozen call-entry scalar snapshot과 call 이후
다시 읽은 memory 값은 구분한다.

```text
AssumeExpr((A,ρ), E op c):
    A' := 기존 domain이 지원하는 sound Assume(A, E op c)
    I  := E op c가 허용하는 sound typed interval
    ρ' := Record(ρ, Key(E), I)
    A' := ReduceEqualExpressionVariables(A',ρ')
    (A',ρ') := BackwardReduceExpression(A',ρ',Key(E),Lookup(ρ',Key(E)))
    return (A',ρ')

PruneFlowExpr((A,ρ), D1 OR ... OR Dm, entry_keys):
    각 Di는 동일한 (A,ρ)에서 독립적으로 시작
    conjunction 내부에서 AssumeExpr를 순차 적용
    결과 A는 기존 numerical join, 결과 ρ는 같은 key의 interval hull로 join
```

Atom이 interval로 표현되지 않으면 I=TOP으로 두어 정보만 포기한다. 예를 들어
signed interval 중간의 0 제외는 기록하지 않는다. 식과 상수의 unsigned 비교는
`E!=0`에서 `[1,UINT_MAX]`, `E u<=10000`에서 `[0,10000]`을 얻어 교차할 수 있다.
Compare predicate의 signedness와 typed range를 그대로 보존한다. 기존 domain이
복합 식 atom을 직접 적용하지 못하더라도 ρ가 그 식의 범위를 보존할 수 있다.
역방향 reduction의 연산별 적용 조건은 DEV_TRANSFER_FUNCTION.md §2.7을 따른다.
Wrap/cast를 보존한 필요조건을 계산할 수 있는 피연산자만 교차 정제하고,
미지원 projection은 생략한다. 충분조건 summary의 atom은 이 생략 때문에 바꾸지 않는다.

22번에서는 Complete인 `guard_pos`의 조건을 다음처럼 적용한다.

```text
K_c(n₀) = E = UAdd_w(ValueId(a),ValueId(b))
ρ_normal(E) := ρ_base(E) ∩ [1,10000]
```

이때 a나 b의 interval을 임의로 좁히지 않는다. 이후 t2=E의 transfer에서
`A(t2)`를 `ρ_normal(E)`와 교차하고 DBZ checker가 그 결과를 사용하게 한다.
t1이 먼저 liveness에서 제거되어도 유효한 E의 fact는 보존한다.

Summary가 Unavailable이거나 적용할 수 없으면 기존 call effect의 ρ invalidation만
수행하고 새 범위를 만들지 않는다. 다른 분기 또는 exception flow에 normal의
fact를 복사하지 않는다. 로컬 ρ map은 FunctionSummary의 새 field가 아니며,
caller-local ValueId를 함수 공통 summary에 저장하지 않는다.

---

## 8. 단계 6 — Exit Memory Summary 적용

### 8.1 Flow별 적용

Normal memory summary는 살아남은 normal flow에만 적용하고 exception memory
summary는 살아남은 exception flow에만 적용한다.

```text
A_{c,X}^pruned = ⊥ ⇒ Memory summary를 적용하지 않음
```

Unavailable이거나 Partial의 보장 영역 포함 증명이 없으면 memory summary를
적용하지 않는다. Complete 또는 정확한 call-entry 포함 증명이 있을 때만 μ_X의
관계를 현재 flow 전체에 적용한다. Domain의 근사 결과만으로 포함을 추정하지 않는다.

### 8.2 Memory entry 변환

```text
Effects_{c,X}^A = {
    κ_c(L) ↦ κ_c(e)
    | L ↦ e ∈ μ_X(g)
}
```

```text
Effects_{c,X}^Σ = {
    θ_c(L) ↦ θ_c(e)
    | L ↦ e ∈ μ_X(g)
}
```

모든 target과 value를 동일한 call-entry snapshot에서 먼저 계산한 뒤 update를
수행한다. 한 entry의 update 결과를 다른 entry의 mapping 입력으로 사용하지 않는다.

### 8.3 기존 IKOS memory refinement

```text
A_{c,X}^post = RefineMemory(A_{c,X}^pruned, Effects_{c,X}^A)
```

다음 조건을 모두 만족하는 relation만 추가 refinement로 적용한다.

- Caller target이 정확히 하나의 유효한 location으로 결정된다.
- Value를 IKOS domain에서 sound하게 표현할 수 있다.
- Target과 value의 type, bitwidth 및 memory range가 호환된다.
- Alias 또는 overlapping write 충돌이 없다.
- 해당 relation이 적용하려는 입력 영역의 flow X 모든 실행에 대해 성립한다.

IKOS의 interprocedural 분석은 callee를 caller state에서 직접 분석하므로
`A_{c,X}^base`에 callee의 memory effect가 이미 포함된다. 현재 구현은 같은 write를
summary로 다시 실행하지 않고 이 기존 결과를 보존한다.

```text
A_{c,X}^post = A_{c,X}^pruned
```

즉 현재 `μ_X(g)`는 caller의 보조 symbolic state와 상위 FunctionSummary 합성에
사용한다. IKOS memory domain에 대한 별도 `RefineMemory` 구현은 후속 범위이며,
구현되더라도 Summary memory로 기존 IKOS memory state 전체를 교체해서는 안 된다.

### 8.4 Symbolic memory 적용

```text
Σ_{c,X}^post
    = ApplySymbolicMemory(Σ_{c,X}^base, Effects_{c,X}^Σ)
```

정확한 relation은 flow별 base symbolic state에 기록한다. Pointer-valued effect의
value는 `Loc`의 pointer-value view 또는 `Null`로 저장한다. Boolean-valued effect는
정확히 표현할 수 있을 때 `BoolExpr`로 전달하고, 기존 memory invalidation을 유지한다.

### 8.5 `TOP` value

Value가 `TOP`이면 기존 IKOS state에는 추가 refinement를 수행하지 않는다. 기존
IKOS 결과가 해당 call context에서 더 정확할 수 있으므로 이를 `TOP`으로
덮어쓰지 않는다.

Symbolic state에서는 해당 target의 정확한 relation을 유지할 수 없으므로 `TOP`을
기록한다.

```text
θ_c(e) = TOP ⇒ μ_{c,X}^post(θ_c(L)) = TOP
```

### 8.6 Target mapping 실패

Target을 정확히 결정할 수 없으면 임의의 strong update를 수행하지 않는다.

- 기존 IKOS memory state는 그대로 유지한다.
- Symbolic state에는 기존 call semantics의 conservative invalidation을 유지한다.
- 기존 invalidation이 충분하지 않으면 가능한 may-alias location을 invalidation한다.

### 8.7 동일 target과 overlapping range

여러 callee location이 동일한 caller location으로 변환되는 경우 다음과 같이
처리한다.

- 모든 value가 동일하면 하나의 update로 결합한다.
- Value가 충돌하면 IKOS에는 추가 refinement를 적용하지 않는다.
- Value가 충돌하면 symbolic target은 `TOP`으로 처리한다.

Memory range가 일부 겹치는 경우 exact byte-level combination을 sound하게 구성할 수
있을 때만 결합한다. 그 외에는 충돌한 range에 같은 fallback을 적용한다.

### 8.8 누락된 entry

Memory summary에 entry가 없다는 사실은 해당 location이 변경되지 않았다는 뜻이
아니다.

```text
L ∉ dom(μ_X(g)) ⇏ μ_{c,X}^post(L) = μ_c^pre(L)
```

빈 memory summary도 모든 memory가 보존됨을 의미하지 않는다. 이는 추가로 적용할
수 있는 memory relation이 없다는 뜻이며, 해당 location은 기존 IKOS call
semantics와 conservative symbolic invalidation 결과를 따른다.

---

## 9. 단계 7 — Call과 Invoke의 Flow 처리

### 9.1 Exception state 구성요소

기존 exception abstract state의 흐름은 개념적으로 다음 요소로 구분한다.

```text
normal
caught_exceptions
propagated_exceptions
```

Summary의 normal flow는 `normal`에 대응한다. `Invoke`의 exception flow는
`caught_exceptions`에 대응하며, 일반 `Call`의 exception flow는 최종적으로
`propagated_exceptions`에 대응한다.

### 9.2 공통 CallBase 처리 위치

공통 처리 순서는 다음과 같다.

```text
기존 callee 분석
→ callee exit invariant 획득
→ normal/exception base flow 분리
→ summary condition pruning
→ summary memory effect 적용
→ Call/Invoke별 exception routing
```

Summary는 callee exit state와 flow별 base state가 준비된 뒤, 일반 `Call`의
caught-to-propagated 변환 또는 `Invoke` successor edge 선택이 이루어지기 전에
적용한다.

### 9.3 일반 Call

`A_{c,N}^post`는 call 다음의 normal statement로 전달한다.
`A_{c,E}^post`는 propagated exception flow로 전달한다.

```text
A_{c,N}^post = ⊥ ⇒ normal continuation 없음
A_{c,E}^post = ⊥ ⇒ propagated exception continuation 없음
```

두 flow는 독립적이므로 하나가 제거되어도 다른 하나는 계속 전달될 수 있다.

### 9.4 Invoke

공통 call 처리 직후 normal 결과는 `normal` component에, exception 결과는
`caught_exceptions` component에 둔다.

Normal successor edge에서는 normal component만 현재 state로 선택하고 caught
exception component를 버린다. Exception successor edge에서는 caught exception
component를 현재 state로 선택하고 normal component를 버린다.

따라서 `Invoke`에는 다음 네 결과가 가능하다.

- Normal edge와 exception edge가 모두 살아 있다.
- Normal edge만 살아 있다.
- Exception edge만 살아 있다.
- 두 edge가 모두 제거된다.

### 9.5 Program exit와 return value

Program-exit flow는 어떤 exception component에도 넣지 않으며 caller successor를
생성하지 않는다.

Return-value summary는 현재 범위에 포함하지 않는다. Normal flow의 기존 IKOS
return 처리는 유지한다. Symbolic call result는 normal flow에서 `TOP`으로
처리하며, exception flow와 program-exit flow에는 return result를 생성하지 않는다.

### 9.6 기존 semantics 보존

Summary가 적용되지 않은 flow는 기존 IKOS flow를 그대로 유지한다.
false/Complete이거나 유효한 Complete 조건과 base state의 교차가 bottom인 경우에만
summary를 근거로 flow를 제거한다. 기존 IKOS가 독립적으로 증명한 bottom은 유지한다.

현재 명세는 singleton callee만 지원하므로 callee별 summary 결과의 join은 수행하지
않는다.

---

## 10. 단계 8 — Conservative Fallback 정책

### 10.1 기본 원칙

Summary 적용은 기존 IKOS call semantics의 결과에 대한 추가 refinement이다.

```text
A_{c,X}^post ⊑ A_{c,X}^base
```

Summary를 안전하게 적용할 수 없으면 기존 base state를 유지한다.

```text
Fallback_A(A_{c,X}^base) = A_{c,X}^base
```

반면 해당 flow의 불가능성이 명시적으로 증명되면 flow를 제거한다.

```text
Summary_X(g) = Known(false, Complete, ∅)
⇒ A_{c,X}^post = ⊥
```

```text
Unavailable       ⇏ ⊥
Unrepresentable   ⇏ ⊥
TOP               ⇏ ⊥
Known(false, Complete, ∅) ⇒ ⊥
Known(false, Partial, ∅)  ⇏ ⊥
```

### 10.2 Summary 유효성 검증

Summary를 적용하기 전에 다음 조건을 확인한다.

- Summary 형식이 올바르다.
- Condition과 memory entry의 타입 정보가 일관된다.
- Parameter-origin atom이 callee signature와 일치한다.
- Location과 offset이 유효하다.
- Summary가 지원되는 분석 규칙으로 생성되었다.
- 충분조건의 인증과 Partial/Complete coverage의 근거가 있다.
- 기존 버전의 필요조건 summary를 충분조건으로 재해석하지 않는다.
- 현재 call context의 제약을 함수 공통 보장으로 저장하지 않았다.

검증에 실패한 summary는 `Known`으로 취급하지 않는다.

```text
Invalid(Summary_X(g)) ⇒ Summary_X(g) = Unavailable
```

잘못된 summary를 근거로 flow를 `⊥`으로 만들지 않는다.

### 10.3 Summary 전체를 적용할 수 없는 경우

다음 경우에는 summary 전체를 적용하지 않는다.

- Callee가 resolve되지 않았다.
- 가능한 callee가 여러 개다.
- Callee body를 사용할 수 없다.
- External call이다.
- Recursive application이다.
- Call signature가 호환되지 않는다.
- Summary가 `Unavailable`이거나 유효성 검증에 실패했다.

Abstract state에는 기존 base state를 유지한다. Symbolic state에는 기존 call
semantics가 요구하는 conservative invalidation을 적용한다.

### 10.4 Condition의 fallback

Summary 생성/합성에서 조건 복원에 실패하면 더 강한 영역을 증명하거나 해당 전체
conjunction의 기여를 생략하여 Partial로 둔다. 필수 atom을 true로 바꾸지 않는다.
κ 치환 실패 시에는 해당 flow의 summary pruning을 생략하고 base state를 유지한다.

Complete 조건의 정확한 κ 치환 후 numerical domain이 특정 atom을 표현하지 못하면
기존 A의 Assume만 no-op으로 처리할 수 있다. ρ에서 표현 가능한 식 범위는
§7.6으로 보존한다. 양쪽 모두 표현할 수 없으면 atom의 적용을 생략한다. 이는 도달 상태의 over-approximation이며
저장한 충분조건 자체를 바꾸거나 Partial 적용의 포함 증명으로 사용하는 것이 아니다.

```text
UnsupportedByDomain(a) ⇒ Assume(A, a)=A
```

유효한 Complete 조건과 state를 교차한 결과가 bottom이면 그 flow는 제거할 수 있다.
Partial 조건의 Assume 결과가 bottom인 것은 그 보장 영역과 겹치지 않는다는 뜻일
뿐이며 실제 flow의 불가능성으로 판단하지 않는다.

### 10.5 Memory entry의 국소 fallback

Memory entry의 적용 실패는 가능한 한 해당 entry에 국한한다.

Target을 정확히 결정할 수 없으면 IKOS에 임의의 strong update를 수행하지 않고,
symbolic state에서는 영향을 받을 수 있는 location을 conservative하게
invalidation한다.

Value를 표현할 수 없으면 IKOS에 추가 refinement를 수행하지 않고 symbolic
target을 `TOP`으로 처리한다.

Alias 또는 overlap 충돌을 sound하게 결합할 수 없으면 충돌한 entry에만 다음
정책을 적용한다.

- IKOS state: 추가 refinement 없음
- Symbolic state: 영향받는 location을 `TOP`으로 처리
- 충돌하지 않는 entry: 계속 적용

### 10.6 Flow 독립성

```text
Failure_N ⇏ Failure_E
Failure_E ⇏ Failure_N
```

한 flow의 summary 적용 실패는 다른 flow의 유효한 summary 적용을 취소하지 않는다.
한 flow 내부에서도 특정 atom 또는 memory entry의 실패 때문에 적용 가능한 나머지
정보를 버리지 않는다. 단, 필수 condition atom의 복원 실패는 전체 conjunction의
보장 인증에 영향을 주며, 나머지 atom만으로 충분조건이라고 주장하지 않는다.

### 10.7 Fallback 처리 순서

1. Summary의 타입, 충분조건 인증, coverage와 적용 가능성을 검사한다.
2. Unavailable/invalid이면 기존 base state를 유지한다.
3. false/Complete이면 해당 flow를 bottom으로 만든다. false/Partial이면 유지한다.
4. Condition을 정확히 caller 문맥으로 치환한다. κ 실패 시 flow 적용을 생략한다.
5. Complete일 때만 전체 flow를 pruning한다.
6. Partial이면 call-entry 전체의 보장 영역 포함을 증명한 경우에만 효과를 사용한다.
   포함 증명이 없으면 미확인 영역을 삭제하지 않고 base 결과를 유지한다.
7. 살아 있는 유효 영역에서만 memory entry를 반영한다.
8. Caller summary 합성 시 θ 실패는 해당 conjunction 기여 생략/Partial로 처리한다.
9. 기존 IKOS 도달 상태는 실제 실행을 모두 포함해야 한다.

### 10.8 핵심 보수성 규칙

```text
Unknown         ⇏ Impossible
Unrepresentable ⇏ Impossible
Missing         ⇏ Unchanged
ProvenImpossible ⇒ ⊥
```

Summary 적용 실패는 기존 IKOS 결과를 보존하거나 symbolic 정보를 약화시키는
방향으로 처리한다. 명시적으로 증명된 불가능성만 flow pruning에 사용한다.

---

## 11. 단계 9 — End-to-End Caller Summary Pruning 알고리즘

### 11.1 입력

```text
Input(c) = (A_c^pre, Σ_c^pre, Targets(c), Summary(g))
```

처리를 시작할 때 `A_c^pre`와 `Σ_c^pre`를 변경되지 않는 call-entry snapshot으로
보존한다.
`K_c`의 actual expression key와 의존 값 identity도 이때 보존하여 callee 실행이나
liveness 제거 이후의 값으로 원래 인자를 다시 해석하지 않는다.

### 11.2 Base flow 생성

먼저 기존 IKOS call semantics와 conservative symbolic call semantics를 수행한다.

```text
(A_{c,N}^base, A_{c,E}^base)
    = ExistingCallSemantics(c, A_c^pre)
```

```text
(Σ_{c,N}^base, Σ_{c,E}^base)
    = ConservativeSymbolicCall(c, Σ_c^pre)
```

### 11.3 적용 가능성 검사와 mapping 생성

`Applicable(c, g, A_c^pre)`가 성립하지 않으면 base flow를 그대로 routing한다.

적용 가능하면 동일한 call-entry snapshot에서 mapping을 생성한다.

```text
κ_c = BuildAbstractMapping(c, A_c^pre)
θ_c = BuildSymbolicMapping(c, Σ_c^pre)
K_c = BuildActualExpressionKeys(c, A_c^pre, Σ_c^pre)
```

### 11.4 Flow별 적용

각 flow의 가용성, 종료 보장 및 coverage를 독립적으로 검사한다. Caller summary의
보장 영역 합성과 IKOS 실제 도달 상태의 refinement는 서로 독립적인 사용처이며,
각각 정확한 θ 치환/인증과 Complete 또는 entry 포함 증명이 필요하다.

### 11.5 통합 의사코드

```text
ApplyCallerSummary(c, A_pre, Sigma_pre):
    entry_A, entry_Sigma := snapshots(A_pre, Sigma_pre)
    entry_keys := BuildActualExpressionKeys(c, entry_A, entry_Sigma)
    base_A, base_S := ExistingCallAndConservativeSymbolicSemantics(c, snapshots)
    post_A, post_S := base_A, base_S
    status_X := Skipped(no applicable summary) for each flow X

    if not Applicable(c, g, entry_A):
        return RouteUsingExistingSemantics(post_A, post_S)

    kappa, theta := BuildMappings(c, entry_A, entry_Sigma)
    for X in {Normal, Exception}:
        if Summary_X(g) is Unavailable or Invalid:
            continue
        (C_X, coverage_X, M_X) := Summary_X(g)

        # 인증된 충분조건만 합성하며 caller의 실제 실행을 삭제하지 않음
        candidate_S_X := ExactOrStrongerConditionSubstitution(C_X, theta)
        forward only certified regions to caller exit-proof construction
        recompute caller coverage independently

        C_A_X := ExactConditionSubstitution(C_X, kappa)
        if C_A_X is Unrepresentable:
            continue
        if coverage_X == Complete:
            (pruned_A_X, pruned_rho_X) := PruneFlowExpr(
                (base_A_X, base_S_X.rho), C_A_X, entry_keys)
        else if Proven(EntailsEntry((entry_A,entry_Sigma.rho), C_A_X)):
            pruned_A_X := base_A_X
            pruned_rho_X := base_S_X.rho
        else:
            continue  # 미확인 영역을 포함한 base 결과 보존

        if pruned_A_X is bottom:
            post_A_X := bottom
            discard symbolic successor for X
            status_X := Pruned
            continue

        effects_A_X, effects_S_X := TranslateMemory(M_X, kappa, theta, snapshots)
        post_A_X := RefineMemory(pruned_A_X, effects_A_X)
        range_refined_S_X := base_S_X with rho=pruned_rho_X
        post_S_X := ApplySymbolicMemory(range_refined_S_X, effects_S_X)
        # memory/binding invalidation에 해당하는 rho fact도 갱신
        status_X := Applied or PartiallyApplied according to valid effects

    return RouteCallOrInvoke(post states and statuses)
```

`ExactOrStrongerConditionSubstitution`에서 stronger 영역은 원래 조건을 함의한다는
증명이 있어야 한다. 실패한 atom 삭제로 영역을 넓히지 않는다. Caller summary를
합성하더라도 이후 statement와 최종 exit까지의 보장이 확인되어야 한다.

### 11.6 Flow routing

일반 `Call`은 살아남은 normal state를 다음 statement로 전달하고 exception state를
propagated exception으로 전달한다.

`Invoke`는 살아남은 normal state를 normal successor edge로, exception state를
exception successor edge로 전달한다.

`⊥`인 flow에는 successor를 생성하지 않는다. Program-exit flow에도 successor를
생성하지 않는다.

### 11.7 결과

```text
Result(c) = (
    A_{c,N}^post,
    Σ_{c,N}^post,
    A_{c,E}^post,
    Σ_{c,E}^post,
    ApplicationStatus_{c,N},
    ApplicationStatus_{c,E}
)
```

`⊥`으로 제거된 flow의 symbolic state는 successor로 전달하지 않는다.

---

## 12. 핵심 불변조건

### 12.1 Refinement

```text
A_{c,X}^post ⊑ A_{c,X}^base
```

Summary 적용은 기존 IKOS 결과에 concrete behavior를 추가하지 않는다.

### 12.2 Soundness

```text
Reach_{c,X} ⊆ γ(A_{c,X}^post)
```

Summary 적용 후에도 실제로 flow `X`에 도달할 수 있는 모든 concrete state가
결과에 포함되어야 한다.

### 12.3 Flow separation

```text
Summary_N(g) → normal flow only
Summary_E(g) → exception flow only
```

Normal flow와 exception flow의 condition, memory effect, 실패 및 pruning 결과를
서로 섞지 않는다.

### 12.4 Entry-snapshot consistency

Parameter mapping, condition substitution 및 memory effect translation은 모두 같은
`(A_c^pre, Σ_c^pre)`를 기준으로 수행한다. Memory update의 적용 순서가 다른
entry의 target 또는 value 계산 결과를 바꾸지 않는다.

### 12.5 불가능성과 불확실성의 구분

```text
Known(false, Complete, ∅) ⇒ A_{c,X}^post = ⊥
Unavailable ∨ Unrepresentable ∨ TOP ∨ false/Partial ⇏ A_{c,X}^post = ⊥
```

### 12.6 Memory 적용 조건

```text
A_{c,X}^pruned = ⊥ ⇒ ¬ApplyMemory_X
```

살아남지 않은 flow에는 memory summary를 적용하지 않는다.

### 12.7 표현식 범위와 동일성

```text
for all s in Reach_{c,X}: EvalMachine(E,s) ∈ γ(ρ_{c,X}^post(E))
```

Local ValueId나 의미가 다른 식을 같은 key로 만들지 않는다. ρ의 생략/무효화는 TOP
방향이며 liveness 제거와 재정의를 구분한다. Checker가 사용하는 A에도 유효한 범위를
반영한다. 전체 함수 summary의 C_X/coverage 계약은 이 interval 불변조건과 별개다.

### 12.8 기존 Call/Invoke semantics 유지

Callee resolution, callee execution, return-value 처리, exception routing,
local-memory deallocation 및 `Invoke` edge selection은 기존 IKOS semantics를
유지한다. Caller summary pruning은 callee exit state가 생성된 이후부터
`Call`/`Invoke` flow가 최종 routing되기 전까지의 refinement 단계로만 동작한다.

---

## 13. 구현 위치와 현재 확장 경계

### 13.1 구현 위치

Summary 적용 지점은 공통 `CallBase` 처리에서 다음 두 시점 사이이다.

```text
callee exit state와 exception flow가 준비된 시점
                         ↓
              caller summary pruning
                         ↓
Call의 caught-to-propagated 변환 또는 Invoke edge 선택
```

이 위치에서 normal flow와 exception flow가 모두 존재하므로 동일한 공통 로직으로
각 flow를 독립적으로 refinement할 수 있다.
다만 ρ 재사용을 위한 actual expression key/snapshot 수집은 call 실행 전 별도
단계에서 수행한다. 이 후처리 지점까지 수집을 미루어 지워진 임시변수를 찾지 않는다.

### 13.2 구현 범위와 인증 한도

조건 전용 `ConditionExpr`는 machine-integer width/sign과 연산 의미를 보존한다.
`exit_proof.cpp`는 경로별 reaching definition으로 조건을 치환하고, 반환에 도달한
인증 경로를 DNF로 합친다. 함수당 block-path 처리 1024회, 경로별 같은 block
방문 16회를 한도로 한다. 미지원 연산·미확인 호출·한도 초과가 있으면 해당 경로를
보장 영역에 넣지 않고 coverage를 Partial로 낮춘다. σ/μ의 값 관계는 기존 flat
merge를 유지하고 ρ는 interval join/widening을 수행한다.

종료 보장 인증의 메모리 지원은 현재 함수에서 할당한 scalar 1개와 offset 0의
타입이 일치하는 store/load, 예외 runtime 규칙으로 검증한 정수 예외 객체의
초기화 및 Writable 전제를 가진 정수 scalar formal-pointer store다(상세 규칙:
DEV_TRANSFER_FUNCTION.md §7.1, §8). 전제로 표현하지 못하는 외부 pointer 접근이나
일반 alias 의존 관계는 Partial이다. No-wrap 연산의 정의역을 기존 조건으로 증명할 수
없으면 넓은 정수식으로 overflow 배제 조건을 보존하고 Partial로 남긴다.
나눗셈과 나머지의 인증 범위는 양의 상수 제수다. 반환값 summary는 생성하지 않는다.

현재 IKOS 본 분석 구현은 interprocedural dynamic inlining에서 callee가 정확히
하나로 결정되고 callee 분석과 summary 저장이 완료된 경우에만 적용한다.

`SummaryStore`에는 현재 분석 프로세스가 직접 생성한 summary만 들어간다. 별도
파일이나 외부 입력에서 summary를 역직렬화하지 않으며, 잘못된 parameter index나
표현 불가능한 operand는 보장 조건 합성에서 해당 conjunction 전체를 버리고
Partial로 표시한다. Complete 조건을 실제 numerical state에 투영하는 단계에서는
표현할 수 없는 atom의 refinement를 생략할 수 있다. 이 투영 결과를 C_X로
재저장하지 않는다.

- Normal summary는 `normal` component에만 적용한다.
- Exception summary는 propagated exception을 `caught_exceptions`로 모은 뒤 해당
  component에만 적용한다.
- 알려진 throw/rethrow/catch runtime은 DEV_TRANSFER_FUNCTION.md §7.1의 필수
  모델을 사용한다. Ordinary Call의 callee exception 조건은 함수 밖으로 합성하고,
  Invoke의 exception 조건은 해당 unwind edge에만 적용한다. 내부 catch 후 반환하는
  영역과 rethrow로 전파되는 영역을 서로 다른 종료 조건으로 수집한다.
- 명시적인 exception-exit을 분석하지 못했거나 함수 안에 보조 symbolic 분석이
  예외를 모델링하지 않는 일반 `Call`이 있는 경우 exception summary는
  Partial이다. exception-exit 부재만으로 일반 `Call`이 외부로 전파할 수 있는
  예외 flow를 false/Complete로 간주하지 않는다.
- `Call`의 caught-to-propagated 변환 및 `Invoke`의 edge selection은 기존 IKOS
  처리를 그대로 사용한다.
- DNF의 각 conjunction은 같은 base state에서 시작하고, 결과를 IKOS join으로
  결합한다.
- Pointer nullity는 actual pointer value에 적용하며, 증명한 Boolean-origin의 nullity도 각 DNF branch 안에서 적용한다.
- Integer atom의 직접 변수 비교는 기존 IKOS machine-integer API로 적용한다.
  Compound typed expression과 상수의 비교는 ρ에 범위를 기록하고 동일한 식을
  나타내는 변수도 정제한다. §7.6 및 DEV_TRANSFER_FUNCTION.md §2.7의 지원
  연산은 피연산자의 기존 state도 역방향 정제한다. 표현할 수 없는 투영은 base state를 유지한다.
- Unsigned affine comparison은 유한 bitwidth에서 항 이항이 동치가 아닐 수 있으므로
  direct parameter/constant 형태만 적용한다.
- Intraprocedural의 context-insensitive call semantics에는 callee별 normal/exception
  base flow가 없으므로 현재 caller pruning을 적용하지 않는다.
- 개발용 적용 로그는 기존 `--trace-ar-stmts` 옵션으로만 출력하며, 옵션 유무는
  pruning 수행 여부에 영향을 주지 않는다.

보조 symbolic 분석에서는 pointer argument가 가리키는 symbolic memory를 먼저
`TOP`으로 invalidation한 다음, 알려진 flow별 `μ_X(g)`만 다시 적용한다. 여러
summary entry가 같은 caller symbolic location으로 치환되면서 값이 충돌하면 해당
location을 `TOP`으로 처리한다.

Wrapping 가능성이 있는 integer add/sub/mul/shift는 affine relation으로 보존하지
않는다. AR의 `no-wrap` 정보가 있는 연산만 affine symbolic expression으로 유지한다.
이는 σ/μ의 규칙이다. 조건 AST와 ρ의 식 identity는 wrapping add/sub/mul도
정확한 width/sign으로 보존한다. 본 분석의 ρ leaf는 internal scalar 값으로 제한하고,
mutable memory cell을 leaf로 공유하지 않는다. 재정의는 의존 식을 무효화하고,
단순 liveness forget은 정규화된 피연산자 식의 범위를 지우지 않는다. 식 깊이 32,
bindings/bounds 각각 256개의 자원 한도를 넘으면 추가 정보를 생략한다.

개정 후에도 NE(`!=`) atom을 summary에서 삭제하지 않는 원칙은 유지한다.
아래 numerical domain 적용은 유효한 Complete 조건이거나 별도로 적용 영역이
증명된 경우만을 뜻한다. 적용 결과를 충분조건으로 재저장하지 않는다.

현재는 NE(`!=`) 조건을 기존 numerical domain이 표현할 수 있는 범위에서만
적용한다. 예를 들어 interval `[0,10]`에는 `x != 0`을 적용하여 `[1,10]`으로
정제할 수 있지만, `[-10,10]`에서 중간의 0만 제외하는 정보는 단일 interval로
보존하지 못하므로 기존 구간을 유지한다. 표현할 수 없는 제외 정보를 별도 상태에
저장하거나 이를 위해 상태를 분리하지 않는다. Summary에 표현 가능한 NE atom을
삭제한다는 뜻은 아니며, 현재 IKOS domain에 적용한 뒤 남지 않는 정밀도를 수용한다.
따라서 benchmark 11·13·19처럼 normal summary가 정확히 `!= 0`을 포함해도
DBZ warning이 남을 수 있으며, 이는 현재 범위에서 허용하는 정밀도 한계이다.

### 13.3 후속 확장 대상

다음 항목은 현재 명세의 보수적인 동작을 유지한 상태에서 후속 연구로 확장한다.

- 복수 callee의 flow별 summary join
- Recursive SCC의 fixpoint summary
- Return-value symbolic summary
- Global variable과 heap object summary
- IKOS memory domain에 대한 별도 summary 기반 `RefineMemory`
- Modifies-set을 이용한 unchanged memory 보존
- Aggregate, vector 및 byte-range memory relation
- 일반적인 Disjunctive abstract domain 또는 path partitioning (nullity Boolean-origin 및 acyclic 분기별 대입의 종료 조건 복원은 현재 범위)
- 복잡한 alias relation과 weak update 정밀화
- Interval과 별도의 zero/non-zero 보조 상태 및 DBZ checker 연계 (Section 13.4)

### 13.4 향후 zero/non-zero 보조 상태 확장

향후 non-zero 정밀도가 필요하면 interval 표현은 유지하면서 별도의 보조 상태를
결합하는 방식으로 확장할 수 있다. 이는 현재 구현 요구사항이 아닌 후속 확장이다.

```text
ν : MachineIntegerVariable → {BOTTOM, Zero, NonZero, TOP}
A_extended = A × ν
FullState_extended = (A_extended, Σ)  // Σ의 ρ는 단일 표현식 interval 유지
```

`Zero`는 값이 0임을, `NonZero`는 0이 아님을 뜻한다. `TOP`은 알 수 없음,
`BOTTOM`은 모순이다. 예를 들어 interval이 `[-10,10]`이어도 `ν(x)=NonZero`를
함께 유지하면 0 제외 사실을 보존할 수 있다. 이는 Σ의 ρ가 저장하는 단일 interval과
구분하는 후속 확장이다. 현재 ρ 도입이 ν까지 구현한다는 뜻은 아니며 별도의
points-to 상태를 도입하는 것도 아니다.

확장 시 다음 사항을 함께 정의해야 한다.

- Flow별 summary 적용에서 `x != 0`은 `NonZero`, `x == 0`은 `Zero`와 교차
  정제한다. 기존 interval과 보조 상태 사이의 모순도 일관되게 처리한다.
- Assignment, load, arithmetic, cast 및 call effect에서 값을 바꾸거나 알 수 없게
  되는 경우 보조 사실을 갱신하거나 무효화한다. Wrapping이나 truncation으로
  non-zero가 zero가 될 수 있으므로 근거 없이 `NonZero`를 전달하지 않는다.
- Merge에서는 모든 도달 가능한 incoming state에서 보장하는 사실만 유지한다.
  `NonZero ⊔ NonZero = NonZero`, `Zero ⊔ NonZero = TOP`이며, 기존 DNF의
  disjunct별 적용과 normal/exception flow 분리 원칙을 동일하게 따른다.
- DBZ checker는 기존 도달 가능성·초기화 검사를 유지하고, interval 조회뿐 아니라
  보조 상태의 `NonZero` 사실도 활용한다. 보조 사실을 저장해 놓고 checker가
  `int_to_interval()` 결과만 검사하면 이 확장의 정밀도 이득을 얻을 수 없다.

이 확장은 우선 `!= 0`을 대상으로 하며, 일반적인 `x != c`의 제외 값 집합이나
임의의 disequality relation까지 현재 범위에 포함하는 것은 아니다.

## 14. 종료 보장 및 caller 안전성 검증 기준

07의 direct pointer guard와 09의 Boolean wrapper는 모두 actual pointer에 non-null을
전달해야 한다. 09에서는 inner guard의 cond==1이 Boolean-origin 치환으로 wrapper의
NonNull(P_0^0)가 되어야 한다. --analyses=nullity에서 두 역참조 warning은 0이어야 한다.

반대로 guard가 없거나 `guard_either(ptr != null, flag)`처럼 null에서도 정상 반환할
수 있으면 warning을 유지한다. Null guard의 반대 encoding, null actual argument,
추가 incoming definition, unsafe cast 및 미지원 pointer offset도 회귀 검증한다.
Trace 옵션 유무와 병렬/순차 분석 모두에서 semantic 결과가 같아야 한다.

### 14.1 개정 계약의 필수 검증

- 실제 정상 종료가 `[1,1000]`인 결정적 guard에서 `[1,500]`은 Partial로 허용하고
  `[1,1500]` 및 비정상 종료 입력을 포함하는 후보는 거부한다.
- Benchmark 21은 DEV_SUMMARY.md §5.1의 parity DNF를 보존한다. 홀수 501은
  보장 조건에서 제외하고 짝수 1000은 포함한다. 약한 `[1,1000]`은 Partial을
  붙여도 유효한 보장 조건이 아니다. `%` 복원 실패 시 인증 가능한 영역만 남겨
  Partial로 두거나 Unavailable로 남긴다.
- 일부 conjunction 인증 실패 시 그 전체 기여를 생략하고 Partial로 표시한다.
  false/Partial 또는 Unavailable 때문에 normal/exception flow를 제거하지 않는다.
- Partial 조건 `[1,500]`을 적용하더라도 실제 정상 반환하는 750의 caller 실행은
  남아야 한다. 그 실행에서 발생하는 오류를 기존 분석이 계속 검사해야 한다.
- Complete는 모든 가능한 flow 입력을 포괄함을 독립적으로 증명한다. 미분석
  call/loop/exception, nondeterministic 종료 및 정의되지 않는 연산을 검증한다.
- Summary의 `%`/NE 표현 보존과 numerical domain 적용을 각각 확인한다. Interval
  join으로 구멍이 메워진 결과를 새 종료 보장 summary로 저장하지 않는다.
- μ relation은 인증된 영역에서만 적용하고 미확인 입력의 memory effect를 보존한다.
- 인증 조건, coverage, 실패 이유 및 실제 적용 범위를 diagnostic에서 구분할 수
  있어야 한다. 출력 문자열에 C_X가 있다는 이유만으로 구현 완료로 판단하지 않는다.

### 14.2 Benchmark 22와 표현식 범위의 필수 검증

- 정상 정의된 guard_pos가 0과 10000 초과를 abort시키는 22번 원형에서,
  a/b 각각의 interval이 TOP이어도 normal flow의 `ρ(UAdd_w(a,b))=[1,10000]`을 얻는다.
- 같은 operand identity로 다시 계산한 합의 numerical interval도 [1,10000]으로
  정제되어 기존 DBZ checker의 warning이 없어야 한다. t1을 먼저 forget하는
  실행 순서에서도 같은 결과를 얻어야 한다.
- Unsigned wrapping 사례를 포함하여 같은 폭의 두 합은 같은 key로 연결한다.
  서로 다른 bitwidth/cast/연산 의미는 증명 없이 같은 key로 만들지 않는다.
- Guard 없는 입력 경로와의 합류, a 또는 b 재대입 및 loop iteration 재실행 시
  과거 범위로 실제 0인 분모를 제외하지 않는다. 필요한 warning을 유지한다.
- σ 값이 모두 TOP인 서로 다른 외부 call 결과나 별도 load 결과를 같은 ValueId로
  합치지 않는다. Call/memory effect로 값이 바뀌는 경우 invalidation을 검증한다.
- DNF/normal/exception마다 독립 ρ를 사용하고 한 branch의 사실을 전체에 강제하지
  않는다. 서로 다른 call context에서 ρ를 전역 range cache로 공유하지 않는다.
- Join의 missing key=TOP, 유한 key 수/깊이 제한, WTO interval widening으로
  soundness와 수렴을 확인한다. σ/μ 안정만으로 ρ fixpoint 완료를 선언하지 않는다.
- ρ의 interval hull을 새 C_X로 내보내지 않고 Partial 보장 조건을 전체 flow의
  range로 사용하지 않는다. Signed non-zero 구멍 정밀도는 기존 수용 정책을 유지한다.

### 14.3 Benchmark 29/30의 원래 검사 보존

- 29번은 DEV_SUMMARY.md §5.3의 DNF를 생성한다. `%` 분기의 guard와 각 대입식이
  올바르게 대응되고, 마지막 `aligned>=2`가 짝수 분기의 `idx₀>=2` 및 홀수 분기의
  `USub_w(idx₀,1)>=2`로 복원되어야 한다. 짝수 분기의 동일 atom은 중복 제거 가능하다.
- 30번은 DEV_SUMMARY.md §5.4의 전체 conjunction을 생성한다. 원래 마지막 검사인
  `UMul_32(step₀,idx₀) u<=9`가 실제로 저장·출력되어야 한다. 상수 operand나
  no-wrap flag를 요구하여 이 atom을 true로 바꾸지 않는다.
- 29번을 `2<=idx₀<8`로만 출력하거나, 30번을 세 개의 입력 범위 조건으로만
  출력하면 보존 요구를 충족하지 못한다. 최종 조건이 우연히 동치이거나 BOA warning이
  없어도 원래 검사를 복원한 구현으로 보지 않는다.
- 두 예제 모두 전제와 정의 가능성, 경로 포괄성을 검증하여 Complete를 증명한다.
  Partial/Unavailable은 복원 실패의 안전한 fallback이며 필수 지원의 완료가 아니다.
- 분기/곱셈 비교를 직접 적용할 수 없는 numerical domain에서도 symbolic summary와
  caller 조건 치환에는 해당 atom이 남아야 한다. 적용 후의 넓은 interval을 원래
  C_X로 덮어쓰지 않는다.
- 두 분기의 대입을 다르게 바꾸거나 곱셈 검사의 상수를 더 작은 값으로 바꾼 변형도
  원래 검사에 따라 요약해야 한다. 원형에서 검사가 중복이라는 사실을 일반적인
  삭제 규칙으로 사용하지 않는다. 해당 조건이 실제 정상 종료 입력을 제한하는
  변형에서 그 입력을 종료 보장 영역에 잘못 포함하지 않아야 한다.
  예를 들어 29번의 마지막 검사를 `aligned<4`로 바꾸면 idx=2,3은 제외하고,
  30번의 검사를 `prod>4`로 바꾸면 (step,idx)=(3,2)는 제외하고 (2,2)는 포함한다.
