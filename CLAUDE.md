# 분석기 수정 명세 — Parameter 전용 정상종료 조건 추출

> 정적 분석기를 수정하여, 함수 분석 중 각 변수를 원래 parameter 값의 선형
> 식으로 매핑(σ)하고, normal exit 의 정상종료 조건을 parameter 식으로 환원한다.
> Part A 배경/목표, Part B 알고리즘 formal spec, Part C 구현 작업 분할.

═══════════════════════════════════════════════════════════════
# Part A. 배경과 목표
═══════════════════════════════════════════════════════════════

## A.1 한 줄 요약
함수 분석 중 각 변수를 원래 parameter 값의 선형 식으로 매핑(σ)하여,
normal exit 의 정상종료 조건을 parameter 식으로 환원한다.

## A.2 배경
- caller 의 argument 와 callee 의 parameter 는 대응 관계가 있다.
- callee 가 정상 종료하는 조건을 parameter 로 표현하면, caller 의 함수 호출
  이후 구문에 pruning 하여 정밀도를 높일 수 있다.
- 이 명세는 그 전제인 callee 내부 "정상종료 조건 → parameter 식" 환원을 다룬다.
  (caller 적용은 후속 작업.)

## A.3 산출물
- 각 함수의 normal exit 에서 정상종료 조건을 parameter 선형 식으로 환원한 결과.
- 분석 종료 후 확인 가능하도록 출력(Part C.4).

## A.4 대상
- 언어: C/C++ (LLVM IR 기반이면 SSA 활용 가능).
- 수정 위치: CFG / transfer function / abstract domain 관련 모듈.
  (정확한 파일은 저장소에서 grep/find 로 탐색.)

═══════════════════════════════════════════════════════════════
# Part B. 알고리즘 Formal Specification
═══════════════════════════════════════════════════════════════

## B.0 전제
- 각 normal exit 노드에는 **분석기가 계산한 정상종료 도달 조건 C_exit**
  (프로그램 변수로 표현) 이 이미 존재한다. 이를 받아 환원한다.
- 분기는 **path-insensitive** (merge 에서 값 뭉침).
- **정확히 선형 식으로 계산되면 유지, 아니면 ⊤** (정확 아니면 TOP).
- 정수 오버플로우는 **UB-free 가정**.

## B.0.1 선행 요구사항 — CFG exit 4종 분리 (분석 전 완료)

우리 분석이 시작되기 **전에** CFG 는 종료 지점이 4종으로 분리되어 있어야 한다.
(변형 시점은 분석 전 사전 변형이든 분석 중이든 무방하나, **우리 분석이 사용하는
시점에는 반드시 분리되어 있어야** 한다. 효율적인 방향으로 택한다.)

종료 지점 4종:
- **function normal exit** : 정상 흐름(return)의 종료가 모인다.
  → 우리가 조건을 환원하는 대상 (B.5 의 C_exit 은 이 노드의 조건).
- **function program exit** : 프로세스를 종료시키는 노드(caller 로 복귀 안 함).
  우선 abort() 로 규정 (이후 exit(), assert 실패, [[noreturn]], noexcept 위반 확장).
- **function exception exit** : 함수 내부 throw 된 예외가 이 함수에서 catch 되지
  않고 밖으로 전파되는 경우. (정적으로 catch 확인되면 exception exit 로 가지 않고
  catch 블록으로 → 함수 내부 소화 후 정상 흐름 합류.)
- **function exit** : 위 셋을 predecessor 로 갖는다. successor 는 caller 의
  after-call node 와 연결.

규칙(반드시 준수):
- **abort() 는 function program exit 로 직결** (normal exit 경유 금지).
- **exit 종류를 value join 하지 않는다** — 종류 구분 유지가 핵심.
  join 하면 구분 소실되어 분석이 무의미해진다.
- **invoke 의 landingpad 구분** (LLVM 기반): landingpad 가 catch 면 예외 소화,
  cleanup 만이면 소멸자 실행 후 resume 재전파 → 여전히 exception exit.

이 분리가 있어야, normal exit 에 모이는 흐름 = 정상 return 조건이 되고,
abort/throw 로 빠진 분기는 애초에 normal exit 에 모이지 않으므로 그 경로들이
배제된 조건이 C_exit 으로 주어진다.

## B.1 기본 정의
- P = {p₁,…,pₖ} : parameter 집합
- p₀ᵢ : 함수 진입 시점 parameter 값 (= caller 의 argument). parameter 는 함수
  내에서 수정될 수 있으므로 기준은 항상 원래 값 p₀.
- E(P) : 원래 parameter 값 p₀ 의 선형 식.  Σ aᵢp₀ᵢ + c  (aᵢ,c 상수; 여러
  parameter 얽힘 허용)
- ⊤ : 정확한 선형 식으로 결정 불가한 값
- σ : Var → E(P) ∪ {⊤} : 변수 → parameter 선형식(또는 ⊤). 정의역은 현재
  scope 에 살아있는 변수로 한정.
- 초기(entry): σ₀ = { pᵢ ↦ p₀ᵢ | pᵢ ∈ P }

## B.2 환원 함수 subst
    subst(e, σ) = e 의 각 변수 v 를 σ(v) 로 치환 후 정규화한 식
                  (local · parameter 구분 없이 모든 변수 치환)
- 결과가 선형 식 Σaᵢp₀ᵢ+c → 그 식 ∈ E(P)
- TOP 조건(B.2.1) 해당 → ⊤
- parameter 도 반드시 치환 (수정되었을 수 있으므로 원래 값 p₀ 로 환원).

### B.2.1 TOP 조건 (정확한 선형식 불가)
1. 변수 하나라도 σ 에서 ⊤ 또는 미등록
2. 비선형: 변수×변수, 변수/변수, 변수%변수, 지수 등
3. 상수 나눗셈/나머지: a/c, a%c (정수 버림)
4. 변수 시프트: a<<b, a>>b (b 변수)
5. function call 포함
6. 좁히는 cast (int→char 등 값이 바뀌는 변환)
7. 그 외 선형식 표현 불가 연산
   (상수배 a*c, 상수 시프트 a<<c 는 선형 → 유지)

## B.3 Transfer 규칙

### (a) declaration  int v;
    v 를 현재 scope 에 등록(매핑 미정). σ 값 불변.
    (int v = e; 는 declaration + assignment 로 취급)

### (b) assignment  v = e
    σ' = σ[ v ↦ subst(e, σ) ]
    v 는 local 또는 parameter. parameter 수정 시 원래 값 p₀ 기준 갱신
    (예: 초기 σ(x)=x₀ 에서 x=x+1 → σ(x)=x₀+1).

### (c) block 종료  { … }
    이 블록에서 선언된 지역변수를 σ 에서 제거(scope 소멸).
    블록 밖 선언 변수의 매핑 변화는 유지.

### (d) branch  if (cond) { S_t } else { S_e }
    σ_t := [[S_t]] σ ,  σ_e := [[S_e]] σ   (else 없으면 σ_e := σ)
    * branch 내부에서 종료(return/exit)하는 경로는 B.5 에서 그 시점 σ 로
      처리(분기 내부 지역변수 포함). merge 는 종료 없이 빠져나온 경로 대상.
    merge (JOIN) — 블록 종료로 지역변수 제거 후, branch 밖 선언 변수 v 만:
        σ'(v) = σ_t(v)   if σ_t(v) = σ_e(v)      # 동일 → 유지
                ⊤         if σ_t(v) ≠ σ_e(v)      # 2개 이상 값 → TOP
    (중첩 분기 / else-if / else 없음 = 이 규칙의 재귀·특수 경우)

### (e) switch — (d) 다중 확장
    모든 case 에서 동일 매핑인 밖 변수만 유지, 다르면 ⊤.

### (f) loop — B.4 참조

### (g) function call
    우변 (v=g(…)) : σ(v)=⊤.
    단독 (g(…);) : 부작용(전역·포인터·참조 수정) 가능 변수를 ⊤.

## B.4 Loop transfer
정의:
- σ_pre : loop 진입 직전 state (중첩 loop 대비 기준)
- guard : loop 반복 조건,  body(·) : 본문 transfer
- M : loop 가 수정하는 변수 중 밖-선언(+parameter) 집합
- F(X) = σ_pre ⊔ body( X ⊓ guard )   (σ_pre 합침 = loop 0회 순회 가능성)
- ∇ widening, △ narrowing

절차:
1. 첫 반복: X₀:=σ_pre, X₁:=F(X₀). 이 1회 중 M 수집·X₁ 계산·widening 판별 동시.
   M ∩ (밖-선언 ∪ parameter) = ∅ → σ:=σ_pre (통과).
2. Widening: Xₙ₊₁ := Xₙ ∇ F(Xₙ) → Xₙ₊₁=Xₙ 까지. 결과 W*.
3. Narrowing: Y₀:=W*, Yₙ₊₁ := Yₙ △ F(Yₙ) → 안정까지. 결과 Y*.
4. 종료 상태: σ_post := Y* ⊓ ¬guard.
5. 환원: 각 v∈M 에 대해 σ_post 에서 v 가 선형 E(P) 로 유일 결정되면
   σ(v):=그 식, 아니면 σ(v):=⊤. (M 이외 변수는 σ_pre 유지.)

연산자 (interval 예):
[a₁,b₁] ∇ [a₂,b₂] = [ a₂<a₁?-∞:a₁ , b₂>b₁?+∞:b₁ ]
[a₁,b₁] △ [a₂,b₂] = [ a₁=-∞?a₂:a₁ , b₁=+∞?b₂:b₁ ]

## B.5 Normal exit 조건과 함수 전체 조건
normal exit 노드 e₁,…,eₙ, 각 eᵢ 에 분석기가 준 조건 C_exit_i:
각 exit 에서 그 위치의 σ_eᵢ 로 환원:
Cᵢ = subst( C_exit_i , σ_eᵢ )
- branch 내부 exit: σ_eᵢ 는 그 경로 상태 → 분기 내부 지역변수 포함 환원.
- merge 후 exit: 분기 내부 지역변수 제외, 갈린 밖 변수는 ⊤.
  함수 전체:
  C_normal = ⋁ᵢ Cᵢ        (모든 normal exit JOIN — 합집합)

═══════════════════════════════════════════════════════════════
# Part C. 구현 작업 분할과 검증
═══════════════════════════════════════════════════════════════

## C.1 핵심 자료구조
- **σ (SymMap)**: 변수 → 선형식(E(P)) 또는 TOP.
    - 선형식 표현: parameter 별 계수 맵 + 상수항. 예 {x:2, y:1, const:5} = 2x₀+y₀+5.
    - scope 관리: 블록 진입/종료 시 지역변수 추가/제거.
- **subst(expr, σ)**: 식의 변수를 σ 로 치환, 선형이면 계수맵, 아니면 TOP.
- 이 둘이 중심. 나머지 규칙은 σ 를 변형/조회.

## C.2 작업 순서 (한 번에 하나씩, 순차 진행)

### Task 0 — CFG exit 4종 분리 (선행)
- 종료 지점을 normal / program / exception / function exit 로 분리(B.0.1).
- abort() → program exit 직결. exit 종류 value join 금지.
- (LLVM 기반) invoke landingpad 의 catch/cleanup 구분.
- 분석 전 사전 변형이든 분석 중이든, 우리 분석 사용 시점엔 분리 완료 상태여야 함.
- 이 Task 완료로, normal exit 노드에서 정상 return 조건(C_exit)을 얻을 수 있음.

### Task 1 — σ 자료구조 + subst (기반)
- SymMap(변수→선형식/TOP) 구현.
- subst: 치환 + 선형 판정(B.2.1 TOP 조건). 상수배·상수시프트는 선형 유지.

### Task 2 — assignment + declaration + scope
- assignment: σ[v↦subst(e,σ)]. parameter 수정 포함(p₀ 기준).
- declaration/block: scope 등록·종료 시 지역변수 제거.

### Task 3 — normal exit 환원 + 출력
- 각 normal exit 에서 그 위치 σ 로 C_exit 환원.
- 함수 전체 C_normal = 합집합. 검증 출력(C.4).

### Task 4 — branch / switch (path-insensitive)
- 경로별 σ 계산 후 merge JOIN(동일→유지, 상이→⊤). 밖-선언 변수만.
- branch 내부 exit 는 그 경로 σ 사용.

### Task 5 — loop
- σ_pre 기준 widening/narrowing fixpoint. M 수집.
- 선형 유일결정 → 환원, 아니면 ⊤.

### Task 6 — function call
- 우변 호출 → ⊤. 단독 호출 부작용 변수 → ⊤.

## C.3 분석기 독립성
- 알고리즘은 특정 분석기에 종속되지 않는다.
- 도메인 의존 연산을 인터페이스로 분리: substitute(선형 치환/판정),
  join/⊤(merge), widen/narrow(loop).
- back edge(loop) 판별, dominator 등 CFG 구조 정보는 프레임워크 제공분 사용.

## C.4 검증 (분석 종료 후 출력)
각 대상 함수에 대해:
1. 함수 식별: 파일 + 함수명.
2. exit 종류 분리 확인: normal / program / exception exit 각각의 노드·조건.
3. 각 normal exit 의 C_exit (환원 전).
4. 환원 과정: 주요 변수의 σ 매핑 (변수 ↦ parameter 식 / ⊤).
5. 각 exit 의 Cᵢ (환원 후 parameter 식).
6. 함수 전체 C_normal (합집합).

## C.5 작업 지침
- 한 번에 하나의 Task 씩 순차 진행.
- 파일 경로 미상이면 먼저 grep/find 로 CFG·transfer function·abstract domain
  관련 위치 탐색.
- 기존 코드 스타일·컨벤션 준수, 기존 테스트 통과 유지.
- 각 Task 의 엣지 케이스(B.2.1 TOP 조건, scope, path-insensitive merge,
  loop σ_pre 기준) 반드시 반영.