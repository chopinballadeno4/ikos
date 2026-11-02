# 실제 프로젝트 6개의 종료 조건 전달 패턴 조사

2026-11-02. **6개 모두에서 연구 대상에 해당하는 검사 → throw/assert 종료 → 후속 사용 구조를 확인했다.** 이전 비교에서 fmt 외 warning 감소가 없었던 것을 “다른 프로젝트에는 패턴이 없다”로 해석하면 안 된다. 실제 소스의 호출 관계 확인과, 그 helper를 이용한 작은 진단 실행을 구분했다. 분석기 구현·DEV 명세·6개 프로젝트 소스는 수정하지 않았다.

## 무엇을 패턴으로 세었는가

함수가 입력 값/포인터를 검사하여 일부 입력에서는 예외를 던지거나 assert/abort로 복귀하지 않고, 정상 복귀한 호출자는 그 입력에 관한 조건을 활용할 수 있는 구조다. 예외를 받는 호출자는 예외가 발생한 입력 조건을 활용할 수도 있다. 단순 `throw` 검색 결과만 세지 않고 실제 호출자와 이후 사용을 함께 확인했다.

오류 코드/false를 **정상 반환**하는 API는 그 사실만으로 C_normal을 성공 입력으로 제한하지 않는다. 반환값을 사용하는 경우와 원래 actual argument를 재사용하는 경우도 다르다. 라이브러리의 반환값 범위는 기존 native 분석으로도 충분할 수 있다.

아래는 각 프로젝트에서 존재를 입증하는 대표 사례다. 유사 helper 전부의 목록이나 전체 missed-warning 개수를 확정한 것은 아니다. 특히 “normal과 catch 양쪽에서 같은 입력을 이용해 기존 warning이 줄어드는 실제 호출 지점”까지 6개 모두에서 확인했다는 의미는 아니다.

## 프로젝트별 실제 코드와 판정

| 프로젝트 | 실제 검사 함수/매크로 | 실제 호출 및 후속 사용 | 판정 |
|---|---|---|---|
| fmt | `to_unsigned(value)`가 음수면 `assert_fail → abort` | `parse_context::advance_to`가 `to_unsigned(it-begin())` 결과를 `remove_prefix`에 전달 | 패턴 있음. 실제 헤더 진단에서 modified가 호출 후 `n>=0`을 증명함. 단 summary coverage에는 별도의 개선점이 있음 |
| PX4 | `Matrix::operator()(i,j)`가 `i<M`, `j<N` assert | `Vector::operator()(i)`가 Matrix accessor를 호출하여 원소 참조를 반환. Matrix의 `swapRows` 등도 accessor로 배열에 접근 | 패턴 있음. integer Matrix 헤더 진단에서 **Complete summary와 caller pruning 작동**. 이전에는 CRC만 비교했으므로 이 패턴을 실행하지 않았음 |
| DuckDB | `DuckDBAssertInternal(condition)`은 false면 throw. `AssertIndexInBounds(index,size)`는 범위 밖이면 noreturn throw helper 호출 | `SelectionVector::operator[]`는 `D_ASSERT(index<capacity)` 후 `sel_vector[index]`. `vector::get`은 bounds helper 호출 후 `std::vector::operator[]` | 핵심 패턴 있음. 공통 assert helper가 Partial이며 숫자 비교 → bool actual의 역치환에도 지원 공백이 있음. 객체의 capacity/size 및 heap 정보도 필요 |
| BehaviorTree.CPP | `checkUpperLimit(const T&)`, `checkLowerLimit(const T&)`가 범위 밖이면 `runtime_error` | `convertNumber`가 helper 호출 후 정수 cast. `Any::convert`가 `convertNumber(...,out)` 호출 후 `out` 반환 | 패턴 있음. **참조의 pointee 값**을 parameter-origin 조건으로 읽는 모델이 현재 범위 밖. 진단에서 normal/exception 조건을 증명하지 못함 |
| rclcpp | `check_if_stringified_policy_is_null(p,kind)`는 null이면 `invalid_argument`, 아니면 p 반환 | QoS 기본값 생성에서 rmw의 문자열 변환 결과를 helper에 전달하고 반환 문자열로 `ParameterValue` 생성 | 패턴 있음. 실제 본문 분리 진단에서 입력 p의 non-null 전달은 미흡하지만 **반환 포인터의 non-null은 증명됨**. 실제 호출자는 반환값을 쓰므로 그 지점을 곧바로 미해결 warning이라고 할 수 없음 |
| tinyxml2 | `ReadBOM(p,bom)`은 두 포인터 assert 후 읽기/쓰기. `StrPair::SetStr(str)`는 assert 후 `strlen`, 복사 | `XMLDocument::Parse`는 `p=ReadBOM(...)` 다음 `*p`. `XMLAttribute::SetName`은 `SetStr(n)` 호출 | 패턴 있음. aggregate/외부 메모리 접근 때문에 summary는 Partial. 입력 p 재사용 진단은 warning이지만, 실제 Parse처럼 **반환 p를 쓰는 진단은 두 분석기 모두 safe** |

### fmt

- [입력 검사: core.h:428](/Users/baesungmin/master/experiment/realworld/fmt/include/fmt/core.h:428)
- [assert 활성화 조건: core.h:394](/Users/baesungmin/master/experiment/realworld/fmt/include/fmt/core.h:394)
- [실제 assert_fail 본문: format-inl.h:49](/Users/baesungmin/master/experiment/realworld/fmt/include/fmt/format-inl.h:49)
- [실제 호출자: core.h:868](/Users/baesungmin/master/experiment/realworld/fmt/include/fmt/core.h:868)

assert 활성 상태에서 정상 복귀 조건은 `value>=0`이다. 실제 헤더와 기존 `format.cc + os.cc` bitcode를 연결한 진단에서 `to_unsigned(n); assert(n>=0)`은 original warning / dev safe였다. 그러나 출력된 C_normal 자체는 `value0>=0 [Partial]`였다. **이 safe 결과를 해당 Partial summary가 caller를 pruning했다는 증거로 해석하면 안 된다.** native 분석의 다른 수정 효과까지 구분하려면 별도 원인 추적이 필요하다. 실제 Complete-only 적용 코드는 아래에 링크했다.

추가로 같은 실제 헤더를 `assert_fail` 선언만 있는 상태로 분석하면 `value0>=0 [Complete]`였다. 실제 본문을 연결했을 때는 `assert_fail` 자체의 C_normal=false [Complete], C_exception=false [Partial]가 상위 호출에서 함께 처리되어 상위 normal coverage도 낮아진다. 이는 정상 flow의 인증을 더 보수적으로 만드는 구현상의 정밀도 손실이다. 이 사례에서는 여전히 dev의 후속 assertion이 safe이므로 **새로운 실제 프로젝트 warning 누락을 발견했다고 주장하지 않는다.**

`NDEBUG`로 실제 헤더 assert를 끄면 두 분석기 모두 후속 assertion warning이었다. 검사 코드가 빌드에서 사라진 경우는 분석기의 패턴 인식 실패가 아니다.

### PX4-Autopilot

- [Matrix 검사 및 원소 참조: Matrix.hpp:105](/Users/baesungmin/master/experiment/realworld/PX4-Autopilot/src/lib/matrix/matrix/Matrix.hpp:105)
- [Vector → Matrix 호출: Vector.hpp:74](/Users/baesungmin/master/experiment/realworld/PX4-Autopilot/src/lib/matrix/matrix/Vector.hpp:74)
- [swapRows의 검사와 접근: Matrix.hpp:571](/Users/baesungmin/master/experiment/realworld/PX4-Autopilot/src/lib/matrix/matrix/Matrix.hpp:571)

실제 Matrix 헤더의 `Matrix<int,4,1>::operator()`를 사용했다. summary는 `i0 u<4 && j0 u<1 [Complete]`. 호출 후 `i<4` assertion은 original warning / dev safe였다. 같은 i로 접근한 진단용 배열의 buffer-overflow 검사도 original warning / dev safe로 바뀌었다. Matrix 내부 검사 자체는 정상 경로에서 이미 원소 접근을 보호한다. 진단은 **추가 caller 사용으로 전달 능력을 검증**하는 것이며 이 caller를 실제 firmware에서 찾았다는 뜻은 아니다.

`NDEBUG`에서는 summary가 `true [Complete]`로 바뀌고 두 분석기의 후속 검사가 모두 warning이었다.

전체 PX4 SDK 없이 헤더만 확인하기 위해 실제 `platforms/common/include`를 사용하고 비어 있는 `px4_boardconfig.h`를 임시 include 디렉터리에 제공했다. 이 integer accessor는 board macro를 사용하지 않는다. 함수 동작 stub은 없으며 실제 보드/firmware 실행 결과는 아니다. 이전 비교의 CRC 파일에는 이 Matrix 패턴이 포함되지 않았다.

### DuckDB

- [공통 bool 검사: assert.cpp:6](/Users/baesungmin/master/experiment/realworld/duckdb/src/common/assert.cpp:6)
- [D_ASSERT의 정의/빌드 조건: assert.hpp:37](/Users/baesungmin/master/experiment/realworld/duckdb/src/include/duckdb/common/assert.hpp:37)
- [실제 검사 후 배열 사용: selection_vector.hpp:161](/Users/baesungmin/master/experiment/realworld/duckdb/src/include/duckdb/common/types/selection_vector.hpp:161)
- [별도의 bounds helper와 호출: vector.hpp:39](/Users/baesungmin/master/experiment/realworld/duckdb/src/include/duckdb/common/vector.hpp:39)
- [범위 밖 throw 본문: memory_safety.cpp:27](/Users/baesungmin/master/experiment/realworld/duckdb/src/common/memory_safety.cpp:27)

가장 직접적인 연구 후보는 다음 실제 구조다.

```cpp
// D_ASSERT(index < capacity)의 실제 호출 형태
DuckDBAssertInternal(bool(index < capacity), ...);
return sel_vector[index];
```

현재 관찰한 helper summary는 `condition0 u==1 [Partial]`, C_exception=false [Partial]이다. 실제 `assert.cpp`를 재컴파일해 연결한 `D_ASSERT(n>0)` 진단에서 dev는 정상 복귀 후 `n>0`, catch에서 `n<=0` 모두 증명하지 못했다. 문자열 formatting, 예외 생성자, cleanup의 복잡성으로 인증 coverage가 제한된다. 연결한 것은 이 실제 번역 단위이며 DuckDB 전체 예외 구현/라이브러리를 연결한 결과는 아니다.

별도로 **helper가 Complete가 되더라도** `bool(index<capacity)==true`를 원래 숫자 비교로 되돌리는 일반 Boolean-origin 지원이 필요하다. 현재 전용 역추적은 pointer-nullity origin이다. 이 부분은 현 명세의 제한을 확장할 항목이다. 실제 배열 안전성에는 capacity가 객체 필드라는 점과 sel_vector의 할당 크기 관계도 연결해야 한다.

`D_ASSERT`는 Debug/FORCE_ASSERT 등의 설정에 의존하고 `DISABLE_ASSERTIONS`는 helper를 무조건 복귀시킨다. `vector` safety도 template 설정 및 DUCKDB_DEBUG_NO_SAFETY/DUCKDB_CLANG_TIDY에 의존한다. 소스 존재와 실제 활성 bitcode를 구분해야 한다.

original은 이 소규모 번역 단위에서도 Darwin RTTI constant import 오류로 실패했다. original warning=0으로 세지 않았다. 이전 전체 CLI 비교도 import 단계에서 양쪽 모두 실패했으므로 해당 비교로 패턴 효과를 판단할 수 없다.

### BehaviorTree.CPP

- [참조 기반 상한 검사: convert_impl.hpp:66](/Users/baesungmin/master/experiment/realworld/BehaviorTree.CPP/include/behaviortree_cpp/utils/convert_impl.hpp:66)
- [하한 검사: convert_impl.hpp:75](/Users/baesungmin/master/experiment/realworld/BehaviorTree.CPP/include/behaviortree_cpp/utils/convert_impl.hpp:75)
- [helper 이후 cast: convert_impl.hpp:184](/Users/baesungmin/master/experiment/realworld/BehaviorTree.CPP/include/behaviortree_cpp/utils/convert_impl.hpp:184)
- [실제 상위 호출: safe_any.hpp:518](/Users/baesungmin/master/experiment/realworld/BehaviorTree.CPP/include/behaviortree_cpp/utils/safe_any.hpp:518)

`const int& from`은 LLVM에서 pointer parameter다. `from>127`의 from은 pointer 자체가 아니라 그 메모리에 들어 있던 int다. 원하는 조건을 나타내려면 `Read(entry_from)` 같은 **입력 메모리 값의 origin**과 유효성/alias 전제가 필요하다. 단순 scalar parameter p00로 바꾸면 pointer와 pointee를 혼동한다.

현재 종료 인증은 일반 formal-pointer load를 허용하지 않는다. 실제 헤더의 `checkUpperLimit<int,int8_t>`와 `convertNumber<int,int8_t>`는 C_normal/C_exception이 false [Partial]이었다. `convertNumber` 이후 범위 assertion, checkUpperLimit의 catch에서 `n>127` assertion 모두 dev warning이다. 현재 명세에 적힌 외부 메모리 읽기 제한에 해당하며, `%`/비선형 연산의 문제가 아니다.

이 API가 반환한 int8_t의 범위는 타입 자체로도 제한된다. 이 진단 결과만으로 실제 프로젝트에서 어떤 overflow warning이 사라져야 한다고 결론 내릴 수는 없다. original은 RTTI import 오류. 이전 전체 dev 실행은 전역 초기화 직후 bottom이라 이 효과를 유효하게 비교하지 못했다.

### rclcpp

- [null 검사와 throw: qos_parameters.hpp:293](/Users/baesungmin/master/experiment/realworld/rclcpp/rclcpp/include/rclcpp/detail/qos_parameters.hpp:293)
- [실제 문자열 소비 caller: qos_parameters.hpp:314](/Users/baesungmin/master/experiment/realworld/rclcpp/rclcpp/include/rclcpp/detail/qos_parameters.hpp:314)
- [추가 실제 null guard: rclcpp_action/src/types.cpp:42](/Users/baesungmin/master/experiment/realworld/rclcpp/rclcpp_action/src/types.cpp:42)
- [그 caller의 stack 주소 actual: server.cpp:409](/Users/baesungmin/master/experiment/realworld/rclcpp/rclcpp_action/src/server.cpp:409)

ROS 2 의존성이 없어서 QoS 헤더 전체를 빌드하지 않았다. 함수 선언/본문을 그대로 추출하여 원래 #line 위치와 SHA256을 보존했고, QosPolicyKind는 opaque enum, stream operator는 실제 시그니처의 외부 선언으로 남겼다. ROS 함수/stream operator의 동작을 가짜로 구현하지 않았다. **따라서 fragment 진단이며 rclcpp 전체 실행이 아니다.**

dev 결과는 호출 후 원래 `p!=nullptr`은 warning, 반환 `result!=nullptr`은 safe, catch의 원래 `p==nullptr`은 warning이었다. 실제 caller는 result로 ParameterValue를 만들기 때문에 입력 p 재사용 진단의 warning을 그대로 실제 caller의 오탐으로 세면 안 된다.

frontend 별도 summary 실행에서는 C_normal=`NonNull(policy_value_stringified0) [Partial]`, C_exception=false [Partial]이었다. 이 실행은 native call-context summary 출력과 구분해서 EVIDENCE.json에 저장했다. ostringstream, 일반 문자열 객체와 예외 구성/정리 호출의 인증이 제한되는 구조다. original은 RTTI import 오류다.

또 `rclcpp_action::convert(...,info)`의 null guard가 있더라도 `convert(uuid,&goal_info)`처럼 stack 주소를 전달하는 실제 caller는 이미 non-null을 안다. 그런 곳에 새 summary가 있어도 warning 감소가 없을 수 있다.

### tinyxml2

- [assert 매크로와 Debug 조건: tinyxml2.h:68](/Users/baesungmin/master/experiment/realworld/tinyxml2/tinyxml2.h:68)
- [ReadBOM의 guard와 메모리 접근: tinyxml2.cpp:392](/Users/baesungmin/master/experiment/realworld/tinyxml2/tinyxml2.cpp:392)
- [실제 caller의 반환값 대입/역참조: tinyxml2.cpp:2566](/Users/baesungmin/master/experiment/realworld/tinyxml2/tinyxml2.cpp:2566)
- [SetStr: tinyxml2.cpp:187](/Users/baesungmin/master/experiment/realworld/tinyxml2/tinyxml2.cpp:187)
- [실제 SetName → SetStr: tinyxml2.cpp:1476](/Users/baesungmin/master/experiment/realworld/tinyxml2/tinyxml2.cpp:1476)

실제 library bitcode와 헤더에 TINYXML2_DEBUG를 켠 구성이다. ReadBOM 및 SetStr의 C_normal은 false [Partial]. ReadBOM은 외부 p의 byte load, bom 쓰기, derived pointer를 다룬다. SetStr은 this의 field, Reset, 문자열 길이와 allocation/copy를 다룬다. 현 scalar 메모리 인증으로 완전하게 요약하는 범위 밖이다.

원래 actual p를 호출 후 재사용하는 진단은 두 분석기 모두 warning이었다. **ReadBOM이 돌려준 result를 사용하는 진단은 두 분석기 모두 safe**였다. 실제 XMLDocument::Parse는 후자 형태다. 따라서 이 실제 call site의 후속 nullity warning을 수정판이 놓쳤다고 판단할 근거는 없다.

추가로 [XMLDocument::Parse(xml,nBytes):2455](/Users/baesungmin/master/experiment/realworld/tinyxml2/tinyxml2.cpp:2455)는 null xml/빈 문서에서 오류 코드를 정상 반환한다. 이 API의 C_normal을 `xml!=nullptr`로 제한하면 안 된다. 조사한 주 라이브러리 두 파일에는 명시적 throw/catch가 없었다. 이 프로젝트에서 확인한 중심 패턴은 Debug assert의 program-exit 분리이며, 라이브러리/할당 함수가 예외를 발생시킬 가능성과는 별개다.

## 발견한 구현/명세 경계와 우선순위

1. **Flow별 coverage 전파를 분리할 필요가 있다.** 명세는 normal Complete / exception Partial 조합을 허용한다. 그런데 [exit_proof.cpp:988](/Users/baesungmin/master/experiment/ikos/frontend/llvm/src/analysis/parameter_symbolic/exit_proof.cpp:988)의 일반 Call 합성과 [1373](/Users/baesungmin/master/experiment/ikos/frontend/llvm/src/analysis/parameter_symbolic/exit_proof.cpp:1373)의 Invoke 합성은 한쪽 coverage 부족을 `incomplete()`로 양쪽에 반영한다. 실제 fmt의 선언-only/본문 연결 비교가 정상 조건의 불필요한 강등을 재현했다. 잘못된 안전 판정을 관찰한 것이 아니라, 이용 가능한 조건을 놓칠 수 있는 정밀도 문제다.
2. **일반 숫자 Boolean-origin이 필요하다.** `D_ASSERT(index<capacity)` 같은 실제 코드를 처리하려면 bool의 true/false에서 원래 정수 predicate로 돌아가야 한다. [현재 native 전용 역추적: parameter_symbolic.cpp:159](/Users/baesungmin/master/experiment/ikos/analyzer/src/analysis/parameter_symbolic.cpp:159)는 nullity용이다. 현 정의의 BooleanValue도 nullity origin 범위이므로 일반화에는 명세 보완이 필요하다.
3. **참조/객체 입력 메모리의 origin을 확장해야 한다.** [외부 load를 인증하지 않는 지점: exit_proof.cpp:1155](/Users/baesungmin/master/experiment/ikos/frontend/llvm/src/analysis/parameter_symbolic/exit_proof.cpp:1155). BTree의 const T&, DuckDB의 capacity/size, tinyxml2의 객체 field와 byte 읽기를 같은 scalar parameter 문제로 볼 수 없다. Readable 전제, 입력값 identity, alias/변경 시 무효화가 필요하다. 현재 명세의 명시적 범위 제한에 해당한다.
4. **실제 후보가 포함된 실행 범위를 확보해야 한다.** PX4 CRC, rclcpp의 ROS 의존성 없는 일부 파일만 실행한 결과는 위 후보를 평가하지 않았다. DuckDB 전체 import 오류, BTree 전역 초기화 bottom도 먼저 구분해야 한다. 해당 환경 장애를 제거한 뒤 실제 caller별 진단을 다시 비교해야 연구의 missed-warning 규모를 셀 수 있다.

[실제 caller 적용: parameter_symbolic.cpp:523](/Users/baesungmin/master/experiment/ikos/analyzer/src/analysis/parameter_symbolic.cpp:523)는 Complete인 flow만 pruning한다. 따라서 `false [Partial]`을 “정상/예외 경로가 없다”로 해석하지 않는다.

## 진단 실행 결과와 재현 자료

모든 실행은 동일한 LLVM14 bitcode를 original/dev에 각각 입력하고 17개 checker를 켰다. interval/inter, opt=basic, globals-init=all, j=1. `__IKOS__`를 정의하여 진단용 assertion이 실제 IR에 남도록 했다. 입력은 __ikos_nondet_* intrinsic으로 초기화했다. 표의 safe/warning은 **추가한 caller assertion 한 지점**이며 프로젝트 전체 개수가 아니다.

| 진단 대상 | 확인한 후속 조건 | original | dev |
|---|---|---|---|
| fmt 실제 헤더 + 실제 core 구현 | normal: n>=0 | warning | safe |
| fmt 실제 헤더, assert_fail 선언만 | normal: n>=0 | warning | safe |
| fmt NDEBUG | normal: n>=0 | warning | warning |
| PX4 실제 Matrix 헤더 | normal: i<4 | warning | safe |
| PX4 NDEBUG | normal: i<4 | warning | warning |
| BTree 실제 convertNumber | normal: -128<=n<=127 | import 실패 | warning |
| BTree 실제 checkUpperLimit | catch: n>127 | import 실패 | warning |
| DuckDB 실제 assert.cpp | normal: n>0 | import 실패 | warning |
| DuckDB 실제 assert.cpp | catch: n<=0 | import 실패 | warning |
| tinyxml2 실제 ReadBOM | normal: 원래 p!=null | warning | warning |
| tinyxml2 실제 SetStr | normal: 원래 p!=null | warning | warning |
| tinyxml2 실제 ReadBOM | normal: 반환 result!=null | safe | safe |
| rclcpp 실제 본문 fragment | normal: 원래 p!=null | import 실패 | warning |
| rclcpp 실제 본문 fragment | normal: 반환 result!=null | import 실패 | safe |
| rclcpp 실제 본문 fragment | catch: 원래 p==null | import 실패 | warning |

모든 assertion은 보고서에 실제 검사 기록이 있는지 확인했다. import 실패는 미실행이며 warning 0이 아니다. Debug assert의 program-exit 효과와 catch의 exception 효과를 혼동하지 않았다. assertion이 warning이라는 사실은 검사 전 입력을 전달하지 못한다는 진단이며 실제 프로젝트의 오탐 위치로 세지 않았다.

- `EVIDENCE.json`: assertion별 상태/위치, 선택한 helper summary, 실행의 전체 집계 및 checker 설정.
- `RUNS.json`: 실행 명령, 입력 bitcode SHA256, 종료 코드, 로그 위치.
- `SOURCES.json`: 실제 6개 소스 commit과 조사 파일 SHA256. tracked 소스 변경 없음.
- `*_probe.cpp`: 실제 헤더/본문을 호출하는 진단 caller. 기존 01~50 benchmark나 그 실행 스크립트에 추가하지 않았다.
- `rclcpp_fragment.cpp`, `rclcpp-extraction.json`: 원래 함수의 복사본과 범위/해시. rclcpp 전체의 빌드 대용이 아니다.
- `run_probes.py`, `collect_evidence.py`: 재현 및 근거 수집. 기존 전체 비교의 bitcode 캐시를 재사용하므로 캐시가 없으면 실제 소스를 다시 컴파일해야 한다.
- raw DB/log/bitcode는 EVIDENCE.json의 `/private/tmp/ikos-pattern-audit-*` 경로에 있다. 임시 경로가 정리되어도 소규모 근거 JSON과 소스 caller는 이 디렉터리에 보존된다.

```sh
python3 /Users/baesungmin/master/experiment/ikos/benchmark/realworld_pattern_audit/2026-11-02/run_probes.py
```

이 조사의 결론은 **“패턴은 존재하고 일부는 처리되며, 나머지는 coverage 인증·Boolean predicate origin·입력 메모리 모델·분석 범위 때문에 효과를 충분히 확인하지 못했다”**다. 6개 프로젝트 전체가 완전하게 요약되었다거나 모든 미감소 warning의 원인을 확정했다는 결론은 아니다.
