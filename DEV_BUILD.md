# 수정판 ikos-dev 버전 확인

수정판의 사람이 관리하는 버전은 저장소 루트 `DEV_VERSION`에 둔다. 최초 버전은
`0.1.0`이며, upstream IKOS 버전 `3.5`와 구분한다. 정식 연구 버전을 올릴 때는
이 파일의 값을 변경하고 아래 install target을 실행한다.

```sh
/Applications/CLion.app/Contents/bin/ninja/mac/aarch64/ninja \
  -C /Users/baesungmin/master/experiment/ikos/cmake-build-debug install
ikos-dev --version
```

`ikos-dev`는 `~/.local/bin/ikos-dev`에서 이 checkout의 `install/bin/ikos`를 가리킨다.
Python launcher는 실제 `settings.ikos_analyzer()` 실행 파일의 `--build-info` JSON을
읽는다. 현재 Git 상태만 읽어 예전 바이너리를 최신 버전으로 표시하지 않는다.

출력 항목:

- `ikos-dev 0.1.0 (IKOS 3.5)`: 수정판 버전과 upstream 버전.
- `Build ID`: source SHA256, Git revision/dirty, compiler/flags, LLVM, build mode,
  feature 설정으로 구분하는 빌드 식별자.
- `Git`: 빌드 당시 commit과 관련 소스의 미커밋 변경 여부(`-dirty`).
- `Built UTC`: 해당 빌드 식별자를 생성한 UTC 시각. 완료/설치 시각은 아니다.
- `Source SHA256`: 빌드 당시 분석기 관련 소스의 내용 hash.
- `Analyzer`: 이 명령이 실제 사용하는 native analyzer의 절대 경로.
- `Source check: MATCH`: 현재 해당 checkout의 분석기 관련 소스가 빌드 당시와 같다.
- `Install check: MATCH`: 현재 설치된 native 바이너리가 build directory의 파일과 같다.

`Source check: STALE`이면 소스 변경 후 재빌드·설치가 필요하다. `Install check:
STALE`이면 빌드 파일과 설치본이 다르므로 install target을 실행한다. 소스 또는
build directory가 없거나 접근할 수 없으면 `unavailable`로 표시하며 최신이라고
단정하지 않는다. Git 정보가 없을 때도 source hash와 버전은 보존된다.

소스 fingerprint는 core/ar/frontend/analyzer의 구현·헤더, Python launcher와
CMake 모듈·주요 CMakeLists, DEV_VERSION을 포함한다. Benchmark, regression test,
문서, `.DS_Store`, 생성된 build/install 파일은 제외한다. 문서만 바뀐 최신 commit과
코드가 바뀐 경우는 다를 수 있으므로 Git 문자열과 두 MATCH 항목을 함께 읽는다.
테스트나 문서까지 저장소 전체가 동일하다는 인증은 아니다.

Build metadata는 매 build에서 다시 계산하며, 내용이 같으면 생성된 C++ 파일과
timestamp를 유지해 불필요한 재링크를 피한다. 버전 정보는 native 바이너리에
포함되므로 파일을 복사하더라도 원래 빌드의 식별자가 남는다.

```sh
install/bin/ikos-analyzer --version
install/bin/ikos-analyzer --build-info
ctest --test-dir cmake-build-debug -R '^analyzer-build-info$' --output-on-failure
```
