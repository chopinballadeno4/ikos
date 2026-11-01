# Normal / exception / program-exit 연구 감사

기존 01~50번과 별도로 실행하는 감사용 벤치마크다. 분석기 소스와 기존 실행
스크립트는 변경하지 않는다. `flow_matrix.cpp`에는 여러 entry point가 있으므로
아래 도구로 실행한다.

```sh
install/libexec/bin/python benchmark/research_audit/run_audit.py
```

선택 옵션:

```sh
install/libexec/bin/python benchmark/research_audit/run_audit.py \
  --ikos install/bin/ikos \
  --output-dir /private/tmp/ikos-my-research-audit \
  --workers 3
```

기본 실행 파일은 이 checkout의 `install/bin/ikos`이다. 기본 결과 디렉터리는
시스템 임시 폴더 아래에 새로 만들며, `report.md`, `results.json`, 실행별 `.log`와
`.db`를 보존한다. 결과 디렉터리는 새 경로를 사용하는 것이 좋다. 오류나 미확인
safe 항목이 하나라도 있으면 종료 코드 1을 반환한다. 현재 발견된 미해결 항목도
실패로 유지하므로 전체 실행이 green이 되는 것이 현재 기대값은 아니다.

## 검증 범위

- checker **8종**: `boa`, `dbz`, `nullity`, `uva`, `sio`, `uio`, `shc`, `prover`.
- 정수: `int8_t/int16_t/int32_t/int64_t`, `uint8_t/uint16_t/uint32_t/uint64_t`,
  `bool`, `size_t`와 같은 폭의 64비트 인덱스, 정수 포인터.
- `float/double`은 지원 여부를 따로 관측한다. 검사 0개를 safe로 세지 않는다.
- normal/exception별 다른 정수 범위, 반대 nullity, 다른 caller-visible store.
- abort와 throw 혼합, wrapper 치환, 정수/사용자 클래스/표준 클래스 예외,
  catch-all, typed catch/rethrow, catch 종료 시 destructor의 효과.
- wrapping 식 identity, 합/상수 나눗셈/나머지/변수 곱셈, truncation,
  피연산자 재정의, 예외 경로의 메모리 갱신에 따른 이전 정보 무효화.
- 불완전한 외부 호출, signed NE의 내부 구멍, 실행 이전 overflow의 진단 보존.

각 양성 사례는 normal과 exception에 모두 도달할 수 있게 만든다. 오류 주입은
`BAD_NORMAL` 또는 `BAD_EXCEPTION`으로 **한 흐름만** 바꾸고 다른 흐름은 그대로
검사한다. 단순히 총 warnings가 0인지 보지 않고 source-line의 `AUDIT` 표식과
해당 checker의 실제 DB check를 대조한다. 누락된 검사와 unreachable을 safe로
계산하지 않는다. 혼합 checker에서도 다른 checker의 safe 판정으로 대신하지 않는다.

Summary 검증은 checker 결과와 독립적이다. Complete 조건의 수학적 의미를 별도
입력 판정식과 경계 표본으로 비교하고, int8/uint8은 전체 256개 값을 검사한다.
메모리 조건은 유효한 stack scalar를 실제 argument로 제공한다. Program exit는
local CFG의 존재와 normal/exception 입력에서의 제외를 검사하며, 조건부
`C_program` summary가 구현됐다고 가정하지 않는다.

유한 테스트이므로 모든 입력, 모든 checker, recursion/동적 dispatch/모든 loop/
일반 alias/모든 C++ 예외 ABI까지 검증했다는 뜻은 아니다. 검증된 summary와
native checker 상태가 일치하는지도 별도로 살펴야 한다. 발견 내용은
[FINDINGS.md](FINDINGS.md)를 참고한다.

## 소멸자 메모리 효과의 최소 재현

```sh
install/bin/ikos benchmark/research_audit/destructor_effect.cpp --analyses=dbz
```

기대값은 0 나눗셈 진단이다. 현재 실행 결과는 safe 1, warnings 0으로 잘못 나온다.
실제 C++ 동작은 UBSan으로 대조할 수 있다. 이 실행은 의도적으로 runtime error로
종료하므로 출력만 확인한다.

```sh
clang++ -std=c++17 -O0 -fsanitize=undefined -fno-sanitize-recover=undefined \
  benchmark/research_audit/destructor_effect.cpp -o /private/tmp/ikos-dtor-witness
/private/tmp/ikos-dtor-witness
```

공식 intrinsic 계약과 맞게 `__ikos_assert`는 `noexcept`로 선언한다. 이를 빼면
LLVM이 assertion을 Invoke로 만들 수 있고 현재 AssertProver는 IntrinsicCall만
검사한다. 또 상수 0 나눗셈은 LLVM이 undef로 바꾸어 checker statement가 사라질
수 있으므로, 음성 대조군에는 입력에 따라 0이 되는 분모를 사용한다.
