# realworld 6개 프로젝트: original IKOS / ikos-dev 비교

2026-11-01에 시작하여 2026-11-02(KST)에 마친 비교다. 이 보고서는 현재 설치된 두 바이너리를 직접 실행한 결과다. **6개 전체의 완전한 warning/safe 비교가 완료되었다는 뜻은 아니다.** 전체 실행 실패, timeout, 전역 초기화 후 모든 경로가 사라진 경우, 의존성 없는 일부 파일의 보조 실행을 구분한다. 확인된 warning 개수 감소는 fmt의 개별 API 비교에 있다.

## 실행 조건과 재현 자료

- original: `/Users/baesungmin/Projects/ikos/install/bin/ikos`, upstream 3.5, **Release**.
- modified: `/Users/baesungmin/.local/bin/ikos-dev` → 이 저장소 `install/bin/ikos`, dev 0.1.0, **Debug**. 정확한 build ID와 native 바이너리 SHA256은 `analyzers-manifest.json`에 보존했다.
- 두 분석기는 각 비교에서 **동일한 LLVM 14.0.6 bitcode 파일**을 받았다. 입력 SHA256을 `RUNS.json`에 보존했다.
- 분석 대상 C/C++ 프로그램은 공통 Debug/O0/`-g` 조건으로 컴파일했다. 이는 분석기 바이너리 자체의 Release/Debug 구분과 별개다.
- 모든 checker 17개 활성화: `-a '*'` = boa, dbz, nullity, prover, upa, uva, sio, uio, shc, poa, pcmp, sound, fca, dca, dfa, dbg, watch. 완료된 paired DB마다 실제 settings의 17개 집합이 동일한지 검증했다.
- 공통 분석 설정: `--domain=interval --proc=inter --globals-init=all --opt=basic -j 1`. liveness, pointer analysis, widening hints, fixpoint cache, libc/libcpp/libikos 모델과 기본 AR pass는 켜 둔다. partitioning은 기본값 `no`다.
- domain, optimization, partitioning 등은 상호 배타적인 선택지이므로 모든 옵션을 동시에 켤 수 없다. 이 실행은 **모든 checker를 같은 고정 설정에서 비교**한 것이며 domain/옵션 조합 전체 탐색은 아니다. interval에서는 UPA의 congruence 정보가 제한된다.
- 라이브러리는 `-e '*'`로 모든 정의를 entry point로 분석한 실행과 개별 API 실행을 구분했다. 실행파일은 `main`을 사용했다. 임의 함수 진입 인자는 제약이 없으므로 해당 개수는 실제 사용 사례 전체의 버그 수가 아니다.
- 제한 시간: 라이브러리 실행 360초, fmt source-root 실행 및 DuckDB CLI/확장 lambda 실행 600초, fmt API별 30초, rclcpp 개별 파일 120초, CRC 240초. tinyxml2 일부 보조 실행은 300초. 정확한 값은 `RUNS.json`.
- Release/Debug가 다르고 일부 실행을 동시에 진행했으므로 **시간 자체로 알고리즘 성능을 비교하지 않는다.** timeout은 완전한 결과를 얻지 못했다는 의미다.
- IKOS 자체 summary 집계 규칙을 사용했다. checker별 raw row 수를 단순히 더하지 않았다. DB 내부 function/memory ID는 함수명과 소스의 allocation 위치로 정규화하여 진단을 대조했다.
- `SUMMARY.tsv`: 모든 실행의 종료 코드, 개수, 시간, 로그 경로. 실패/timeout 개수는 `N/A`다.
- `RUNS.json`: 정확한 실행 명령, bitcode SHA256, timeout. `completed-comparisons.json`: 완료된 35개 paired 실행의 위치별 상세 차이.
- `CHECKER_RAW.tsv`: 각 완료된 paired 실행에서 17개 checker별 raw safe/unsafe/warning/unreachable row 수. 호출 문맥별 중복이 있으므로 이 수를 프로젝트 summary와 혼동하거나 단순 합산하면 안 된다. 적용 대상 instruction이 없는 checker는 0이다.
- 원본 DB, bitcode, 컴파일 명령, 빌드/분석 로그: `/private/tmp/ikos-realworld-20261101-pou0ms1d`. **임시 디렉터리이므로 재부팅/정리 시 삭제될 수 있다.** 소규모 보고서와 비교 메타데이터는 이 폴더에 별도로 보존했다.
- DuckDB의 개별 중간 bitcode는 최종 연결 후 `*.bc.gz`로 복원 가능하게 압축했다(약 7.95 GB → 2.25 GB). 최종 `duckdb/cli.bc`와 보조 비교에 쓰인 `duckdb/bitcode/0003.bc`는 그대로다. 다른 개별 파일을 다시 연결하려면 `gunzip bitcode/*.bc.gz`로 먼저 복원한다. 정확한 수치는 `cache-compression.json`.
- 이번 비교에서 분석기와 프로젝트 소스는 수정하지 않았다. 빌드가 만든 BehaviorTree의 ignored `.clangd` 외 checkout의 git status는 기존 상태 그대로다.

| 프로젝트 | 소스 commit |
|---|---|
| BehaviorTree.CPP | `90ce0a2ce97b` |
| PX4-Autopilot | `1eccb0876390` |
| duckdb | `5a17f33fea41` |
| fmt | `0fc31633e7d5` |
| rclcpp | `55ec7931a7a1` |
| tinyxml2 | `8224e427b655` |

## fmt: warning 감소 확인, 프로젝트 전체 합계는 미확정

기본 라이브러리 전체 실행은 양쪽 모두 360초 안에 끝나지 않았다. 이후 `format.cc + os.cc`를 연결한 동일한 bitcode의 강한 외부 정의 API 50개를 각각 진입점으로 비교했다. 양쪽 모두 완료된 것은 **27/50개**, warning 개수가 다른 것은 **9개**다. 일부 API는 timeout이며 전체 프로젝트의 합계로 일반화하지 않는다. `fmt-c.cc`를 포함한 전체 실행은 별도 `library-all` profile이다.

| 진입 함수 | original warning | dev warning | safe original / dev | unsafe original / dev |
|---|---:|---:|---:|---:|
| `_ZN3fmt3v126detail5printEP7__sFILENS0_17basic_string_viewIcEE` | 105 | 102 | 952 / 952 | 8 / 8 |
| `_ZN3fmt3v1213buffered_file5closeEv` | 111 | 108 | 950 / 950 | 8 / 8 |
| `_ZNK3fmt3v1213buffered_file10descriptorEv` | 104 | 101 | 945 / 945 | 8 / 8 |
| `_ZN3fmt3v124file5closeEv` | 109 | 106 | 949 / 949 | 8 / 8 |
| `_ZNK3fmt3v124file4sizeEv` | 105 | 102 | 949 / 949 | 8 / 8 |
| `_ZN3fmt3v124file4readEPvm` | 110 | 107 | 954 / 954 | 8 / 8 |
| `_ZN3fmt3v124file5writeEPKvm` | 108 | 105 | 955 / 955 | 8 / 8 |
| `_ZN3fmt3v124file6fdopenEPKc` | 112 | 109 | 952 / 952 | 8 / 8 |
| `_ZN3fmt3v1211getpagesizeEv` | 101 | 98 | 945 / 945 | 8 / 8 |

9개 API에서 반복된 차이는 공통 내부 함수 `basic_format_args::type(int)`의 다음 **서로 다른 검사 3개**다. API 수를 곱하여 27개의 고유 warning이 해결되었다고 합산하지 않는다.

| 위치 | 검사 | original | dev |
|---|---|---|---|
| `fmt/include/fmt/core.h:2573:23`, `index * 4` | signed overflow | warning | safe |
| 같은 위치 | signed underflow | warning | safe |
| `fmt/include/fmt/core.h:2575:45`, `desc_ >> shift` | shift count | warning | safe |

예를 들어 `fmt::detail::print(FILE*, string_view)` 실행의 statement 847에서는 overflow/underflow 각각 original `safe 1 + warning 2` 문맥이 dev `safe 3` 문맥으로 바뀌었다. statement 853의 shift-count도 같다. 검사 누락이나 그 지점 전체의 unreachable 처리로 없어진 것이 아님을 raw DB에서 확인했다. 전체 safe 수가 그대로인 것은 원래도 해당 statement에 safe 문맥이 있었기 때문이다. 같은 scope의 unsafe 8개도 그대로다. 이 비교만으로 다른 진단의 실제 버그 여부나 수정판 전체의 soundness가 증명되지는 않는다.

**2026-11-02 후속 원인 확인:** 위 세 검사 감소는 함수 종료 summary의 직접 효과로 분류하지 않는다. `get(id)` 안의 `unsigned(id)<15` 조건을 원래 signed id로 역전파하는 expression-range 개선으로 같은 차이를 `--proc=intra`에서도 재현했다. 실제 감소 문맥의 dev index는 `[0,14]`였다. [호출 경로·invariant·대조 실행 상세](/Users/baesungmin/master/experiment/ikos/benchmark/realworld_comparison/2026-11-01/FMT_WARNING_REVIEW.md).

## tinyxml2: 개수는 동일, 일부 warning의 범위 정보 차이

기본 RTTI 구성은 original이 Darwin의 tagged RTTI 상수식(`inttoptr(add(ptrtoint(...), INT64_MIN))`)을 import하다 `unexpected llvm constant expression`으로 실패한다. dev는 분석을 완료한다. 실패한 original을 warning 0개로 취급하지 않는다.

세 소스에서 `dynamic_cast`/`typeid` 사용이 없음을 확인하고, **두 분석기에 동일한 `-fno-rtti` 구성**을 별도 적용하여 보조 비교했다. 원래 구성과 혼합하지 않는다. 라이브러리 전체 함수 실행과 라이브러리+`xmltest.cpp`의 `main` 실행 모두 safe/unsafe/warning/unreachable 개수는 동일했다. `xmltest`는 safe 1348, unsafe 6, warning 106, unreachable 2451이다.

단, `xmltest-no-rtti-main`의 10개 source/check 위치에서 warning의 수치 정보가 달라졌다. 예를 들어 `tinyxml2.cpp:2418`의 접근 offset 상한은 original `UINT64_MAX - 1`에서 dev `INT64_MAX`로 좁혀졌고, `LoadFile`의 allocation 크기 상한은 `UINT64_MAX`에서 `INT64_MAX + 1`로 바뀌었다. 모두 유한한 정수 경계이며 무한대가 아니다. 해당 진단은 계속 warning이다. `completed-comparisons.json`에 전체 차이를 보존했다.

## BehaviorTree.CPP: warning 0을 개선으로 해석하면 안 됨

라이브러리 42개 소스와 bundled tinyxml2/minitrace를 연결했고 예제 `t01_first_tree_static`의 실제 `main`도 연결하여 실행했다. ZeroMQ가 없어 Groot interface는 끄고 SQLite logging은 켰으며 unit test는 제외했다. SQLite CMake target이 전파한 SDK include path는 libc++ header 순서를 깨므로 중복 SDK `-isystem`을 제거했다. `script_tokenizer.cpp`는 누락된 직접 표준 include를 보완하는 `-include vector`로 컴파일했다. 소스 파일은 수정하지 않았다.

original은 RTTI 상수식 import 단계에서 실패한다. dev의 전체 함수 실행은 safe 0 / warning 0 / unreachable 79110, 첫 트리 예제 `main`은 safe 0 / warning 0 / unreachable 182다.

`--display-inv=all`로 다음을 직접 확인했다:

```text
Invariant after global variable static initialization:
(normal=⊥, caught_exceptions=⊥, propagated_exceptions=⊥)
```

즉 **전역 변수 정적 초기화 직후부터 상태가 bottom**이라 실제 main 경로 분석의 유효성을 확인할 수 없다. 이것을 안전성 개선으로 분류하지 않는다. 기본 정책인 `--globals-init=skip-big-arrays`로 다시 실행해도 정적 초기화 직후 bottom과 main의 unreachable 182개는 같았다. 원래 main 초기 상태 대신 새 초기 상태를 쓰는 `--no-init-globals=main` 보조 진단에서는 safe 933 / unsafe 1 / warning 1 / unreachable 427이 나왔다. 이 옵션은 의미를 바꾸므로 기본 비교 결과와 합치지 않는다. 어떤 global initializer가 최초 bottom을 만드는지는 이번 비교에서 확정하지 않았다.

## PX4-Autopilot: 전체 빌드 불가, CRC 파일 보조 비교 동일

`px4_sitl_default` CMake 구성은 checkout의 누락 submodule들, Python `kconfiglib/menuconfig` 부재로 실패했다. 준비된 빌드 환경은 없다고 사용자에게 확인했다. 전체 firmware 분석 결과는 없다.

외부 의존성이 없는 실제 `src/lib/crc/crc.c`는 원래 플랫폼의 visibility용 `__EXPORT` 정의만 빌드 인자로 제공해 컴파일했다. 가짜 함수/동작 stub은 추가하지 않았다. 모든 정의를 대상으로 original/dev 모두 **safe 65 / unsafe 0 / warning 23 / unreachable 0**이며 정규화한 진단도 동일했다. 이것은 CRC 파일 결과이며 PX4 전체 결과가 아니다. 저장소의 `AGENTS.md`에 따라 `.agents/skills/build-px4-macos/SKILL.md`를 확인했다. 해당 스킬의 firmware Docker 작업 대신 이 분석에는 POSIX SITL 구성을 사용했으며 firmware를 만들거나 업로드하지 않았다.

## rclcpp: ROS 2 부재, 일부 파일에서만 비교 가능

`ament_cmake_ros`와 rcl/rmw/rosidl 등 ROS 2 의존성이 없어 CMake 전체 구성이 실패했다. 준비된 ROS 2 환경도 없다. 4개 패키지의 실제 구현 소스 113개를 ROS stub 없이 독립 컴파일 시도했고 **6개만 성공**했다. 6개를 연결하면 original은 RTTI import 오류, dev는 safe 834 / unsafe 1 / warning 678 / unreachable 3이다. 전체 rclcpp 결과가 아니다.

개별 파일에서 양쪽 모두 분석이 완료된 범위는 아래와 같다. 정규화한 진단도 동일했다.

| 파일 stem | safe original / dev | unsafe original / dev | warning original / dev |
|---|---:|---:|---:|
| `event` | 93 / 93 | 0 / 0 | 57 / 57 |
| `future_return_code` | 480 / 480 | 1 / 1 | 306 / 306 |
| `global_event_id_provider` | 0 / 0 | 0 / 0 | 0 / 0 |
| `rmw_implementation_specific_payload` | 10 / 10 | 0 / 0 | 12 / 12 |

`global_event_id_provider`는 생성된 검사 자체가 0개이므로 안전성 검증의 증거로 삼지 않는다. 나머지 `managed_entity`와 `write_preferring_read_write_lock`은 original RTTI import 실패, dev는 완료했다. 누락된 ROS header/compiler 오류와 113개 파일 목록은 임시 결과의 `standalone-compile-results.json` 및 `standalone/*.log`에 있다.

## DuckDB: 전체 CLI 빌드와 분석 상태

CLI dependency graph에서 필요한 **1892개** 번역 단위를 Clang 14 bitcode로 컴파일했다. 분석용 비-unity 빌드에서 `partitioned_sort.cpp`의 불완전 타입 오류를 해결하기 위해 ColumnDataCollection / physical_operator_states / SortedRun 헤더를 `-include` 인자로 제공했다. 동일한 결과 bitcode를 양쪽에 사용하며 소스 수정은 없다. core_functions를 정적으로 연결한 CLI 구성이며 추가 extension의 모든 배포 조합을 분석한 것은 아니다.

전체 `cli-main`은 **양쪽 모두** bitcode를 AR로 옮기는 단계에서 `unsupported llvm type`, exit 5로 실패했다. original은 약 171초, dev는 약 599초에 오류를 반환했다. dev 실행은 600초 제한 직전에 **오류로 종료되었으며 timeout이 아니다.** 어떤 LLVM type이 최초 원인인지는 이 오류 메시지만으로 특정하지 않았다. DuckDB 프로젝트 전체의 warning 개수 차이는 확정할 수 없다. 빌드 모드도 달라 이 시간으로 분석 알고리즘의 성능 차이를 단정하지 않는다.

별도 `extension/core_functions/lambda_functions.cpp` 번역 단위는 600초로 제한을 늘린 실행에서 양쪽 모두 완료했다. original은 **safe 7977 / unsafe 14 / warning 7264 / unreachable 166**, dev는 **safe 7980 / unsafe 14 / warning 7264 / unreachable 166**이다. 차이는 libc++ `__split_buffer:308:28`의 서로 다른 세 template instantiation에 pointer-overflow `safe` 기록이 추가된 것이다. warning이 safe로 바뀐 세 건이라고 해석하면 안 된다. warning 개수는 같으며 단일 번역 단위 결과이므로 전체 DuckDB 합계와 합치지 않는다.

## profile별 실행 결과

| 프로젝트 / profile | original | dev |
|---|---|---|
| tinyxml2 / `library-all` | 실패, exit 5 (개수 집계 제외) | safe 5052, unsafe 1, warning 4853, unreachable 53 |
| tinyxml2 / `xmltest-main` | 실패, exit 5 (개수 집계 제외) | safe 1348, unsafe 6, warning 106, unreachable 2451 |
| tinyxml2 / `library-no-rtti-all` | safe 5052, unsafe 1, warning 4853, unreachable 53 | safe 5052, unsafe 1, warning 4853, unreachable 53 |
| tinyxml2 / `xmltest-no-rtti-main` | safe 1348, unsafe 6, warning 106, unreachable 2451 | safe 1348, unsafe 6, warning 106, unreachable 2451 |
| BehaviorTree.CPP / `library-all` | 실패, exit 5 (개수 집계 제외) | safe 0, unsafe 0, warning 0, unreachable 79110 |
| BehaviorTree.CPP / `example-first-tree` | 실패, exit 5 (개수 집계 제외) | safe 0, unsafe 0, warning 0, unreachable 182 |
| duckdb / `cli-main` | 실패, exit 5 (개수 집계 제외) | 실패, exit 5 (개수 집계 제외) |
| duckdb / `lambda-functions-extended` | safe 7977, unsafe 14, warning 7264, unreachable 166 | safe 7980, unsafe 14, warning 7264, unreachable 166 |
| fmt / `library-all` | timeout (개수 집계 제외) | timeout (개수 집계 제외) |
| fmt / `core-source-roots` | timeout (개수 집계 제외) | timeout (개수 집계 제외) |
| PX4-Autopilot / `crc-all` | safe 65, unsafe 0, warning 23, unreachable 0 | safe 65, unsafe 0, warning 23, unreachable 0 |
| rclcpp / `standalone-all` | 실패, exit 5 (개수 집계 제외) | safe 834, unsafe 1, warning 678, unreachable 3 |

`N/A`/실패/timeout은 warning 0이라는 뜻이 아니다. `SAFE`라는 출력이 있더라도 모든 경로가 unreachable인 BehaviorTree 결과는 별도로 점검해야 한다.

## 재실행 예

아래는 캐시된 동일 bitcode로 fmt의 차이가 확인된 entry point를 양쪽에 다시 실행하는 예다. 새 label을 사용해 기존 DB를 보존한다.

```sh
IKOS_REALWORLD_RESULTS=/private/tmp/ikos-realworld-20261101-pou0ms1d \
python3 /Users/baesungmin/master/experiment/ikos/benchmark/realworld_comparison/2026-11-01/run_compare.py analyze fmt \
  --bc /private/tmp/ikos-realworld-20261101-pou0ms1d/fmt/core.bc \
  --entry _ZN3fmt3v126detail5printEP7__sFILENS0_17basic_string_viewIcEE \
  --label reproduce-print --timeout 600
```

다른 profile의 명령은 `RUNS.json`을 사용한다. 임시 bitcode가 없으면 각 프로젝트의 `configure-command.json`, `compile_commands.json`, `bitcode/*.command.json`, compatibility retry 기록에서 재생성해야 한다. 원래 빌드 설정, RTTI-disabled 보조 설정, 초기화 생략 진단을 섞지 않아야 한다.
