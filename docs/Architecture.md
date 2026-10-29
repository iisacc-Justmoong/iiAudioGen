# iiAudioGen 실제 생성 구조

`iiAudioGen` 0.3.0은 로컬 AudioLDM·AudioLDM2 모델과 프롬프트를 이용해 오디오를 생성하는 C++23 SDK이다. 공개 API와 프로세스 제어는 C++로 구현하며, 모델 연산은 공식 Diffusers·PyTorch 런타임에서 수행한다. Qt는 사용하지 않는다.

생성 라이브러리는 다른 SDK를 링크하지 않으며 float PCM을 호출자에게 반환한다.
WAV 직렬화 CLI는 독립 소비자 프로젝트에서 iiAudioGen과 iiFileProvider를 각각
연결하고, 저장 파일의 단계적 게시를 iiFileProvider에 위임한다.

## 모델 로딩과 생성 절차

1. `Generator::loadModel(modelPath)` rejects empty paths, embedded NUL characters and `.onnx` model files before calling the backend. GGUF, safetensors and other paths are forwarded; the backend validates the actual format and architecture. Model directories may contain configuration and tokenizer assets.
2. 기본 Diffusers 백엔드는 실제 로컬 모델 디렉터리를 확인하고, 셸을 거치지 않는 별도 Python 프로세스를 시작한다.
3. 워커가 `model_index.json`의 클래스 이름을 확인하고 AudioLDM 또는 AudioLDM2 파이프라인을 선택한다. 로컬 파일만 사용하고 Safetensors 가중치를 요구한다. 사용자 모델의 임의 Python 코드는 실행하지 않는다.
4. 텍스트 인코더, 확산 모델, VAE, 보코더 등 파이프라인 구성요소가 실제로 로딩된 뒤에만 성공을 반환한다. 파이프라인은 같은 워커에 유지된다. 모델 교체가 실패하면 기존 파이프라인을 유지한다.
5. `generate(request)`가 프롬프트·길이·선택적 시드를 전달한다. 파이프라인은 텍스트 조건에 따라 잠재 표현을 생성하고, 확산 단계와 디코딩·보코더를 거쳐 파형을 반환한다.
6. 모델의 실제 샘플 레이트를 읽고, 필요하면 `scipy.signal.resample_poly`로 요청 레이트에 변환한다. 요청한 프레임 수로 정리한 뒤 PCM을 반환한다. 모노 모델의 스테레오 요청은 채널 복제이다.
7. C++ 백엔드가 응답 길이, 프레임 수, 샘플 레이트, 채널 수와 모든 샘플의 유한성을 확인한 뒤 `GenerationResult`를 반환한다.

모델을 실제로 로딩한 파이프라인에 프롬프트를 전달하는 절차는 [AudioLDM2 공식 문서](https://huggingface.co/docs/diffusers/api/pipelines/audioldm2)의 사용 패턴을 따른다. 현재 백엔드는 Diffusers 모델 디렉터리를 지원하며 단일 가중치 파일, 임의 모델 구조, Hub ID와 자동 다운로드는 처리하지 않는다.

## API와 오류

`GenerationRequest`는 프롬프트, 초 단위 길이, 샘플 레이트, 채널 수와 선택적 64비트 시드를 담는다. `GenerationResult`는 채널이 번갈아 배치된 부동소수점 PCM과 실제 출력 형식을 담는다.

`ModelLoadOutcome`은 `std::expected<void, Error>`, `GenerationOutcome`은 `std::expected<GenerationResult, Error>`이다. 오류 종류는 `InvalidRequest`, `BackendUnavailable`, `GenerationFailed`, `InvalidModel`, `ModelLoadFailed`, `ModelNotLoaded`이다. 기본 백엔드에서는 로딩 전 생성 호출을 거절한다.

확장자 검사는 모델 바이트의 유효성이나 구조 호환성을 보증하지 않는다. 주입된 `Backend`는 실제 가중치 형식과 자신이 지원하는 모델 구조를 검증해야 한다. 기본 Diffusers 백엔드는 단일 파일이 아니라 모델 디렉터리만 받으며 `use_safetensors=True`로 가중치를 로딩한다.

`Generator{}`는 기본 Diffusers 백엔드를 만든다. 기본 설정은 PATH의 `python3`, CPU, 50회 추론 단계, 요청당 5분 시간 제한이다. `DiffusersOptions`와 `createDiffusersBackend()`로 Python 실행 파일·장치·단계 수·시간 제한을 명시할 수 있다. 빈 백엔드가 필요한 경우 `Generator{nullptr}`를 사용한다. 외부 `Backend` 구현 주입도 유지한다.

구체적인 백엔드는 최대 60초, 8000..192000Hz, 1·2채널을 지원하고 프롬프트·모델 경로 크기를 제한한다. 리샘플링은 출력 형식 변환이며 원본 모델의 주파수 대역이나 생성 품질을 높이지 않는다. CPU, MPS와 CUDA 선택을 제공하지만 장치의 실제 지원 여부는 런타임에서 확인한다.

## 프로세스·수명·오류 처리

워커 소스는 CMake가 라이브러리에 포함한다. 설치 후 소스 디렉터리나 별도 워커 파일 탐색에 의존하지 않는다. Python과 ML 패키지는 호스트가 관리하는 런타임 의존성이다. `loadModel()` 전에는 Python 프로세스나 가중치를 로딩하지 않는다.

POSIX는 `posix_spawnp`와 로컬 소켓, Windows는 `CreateProcessW`와 파이프를 사용한다. 프로그램 인자와 프롬프트에 셸 해석을 적용하지 않는다. 워커 표준 출력은 길이로 구분된 이진 프로토콜에 예약하고 일반 로그는 별도로 처리한다. 요청은 백엔드별 뮤텍스로 직렬화한다.

응답은 최대 128MiB이며 잘못된 프레임, 잘못된 오류 코드, 불완전 응답, 비유한 PCM과 시간 초과를 성공으로 취급하지 않는다. 손상되거나 종료된 워커는 정리하고 로딩 상태를 해제한다. 다음 모델 로딩에서 새 워커를 시작할 수 있다. 파이프가 끊어졌을 때 호스트가 SIGPIPE로 종료되지 않도록 처리한다.

생성기는 공유 포인터로 백엔드 수명을 보유한다. 백엔드 소멸 시 자신의 워커 연결을 닫고 최대 약 1초의 정상 종료 시간을 준 뒤 남은 프로세스를 정리한다. 종료·시간 초과 시 다른 앱의 작업이나 워커를 종료하지 않는다. 실행 중 요청 취소 API는 현재 제공하지 않는다.

## 패키지와 검증 범위

CMake 타깃은 `iiAudioGen::iiAudioGen`이며 패키지 버전·공유 라이브러리 ABI는 0.3이다. 공유·정적 빌드를 제공하고 출력은 `build/` 아래에 둔다. C++ 라이브러리를 빌드할 때 Python 개발 헤더나 Qt가 필요하지 않다. 테스트에는 Python 3.10 이상, 추론에는 `runtime/requirements.txt`의 패키지가 필요하다.

AudioLDM2의 GPT-2 생성 경로는 현재 검증한 Transformers 5와 호환되지 않았다. 실제 모델 검증을 위해 전용 환경에서 Transformers 4.48.3, Diffusers 0.35.2, Hugging Face Hub 0.36.0을 함께 고정한다. 패키지 업그레이드는 실제 모델 테스트를 다시 수행한 후 적용해야 한다. 기존 SDK의 런타임을 변경하거나 공유 환경에 의존하지 않는다.

기본 테스트는 SDK 계약, 워커 모델 검사, 기존 모델 유지, 프로토콜, PCM 검증, 시간 초과·프로세스 종료·재시작을 확인한다. 실제 모델 테스트는 별도 가중치를 로딩하고 하나의 워커에서 여러 프롬프트와 시드를 실행한다. 두 검증 종류를 구분하며, 수치 검증을 주관적인 음질·프롬프트 일치도 평가로 간주하지 않는다.

현재 실제 실행 증거는 macOS arm64 CPU이다. Windows·Linux의 빌드·실행, MPS·CUDA 실행과 다른 모델 체크포인트의 품질은 별도 검증 대상이다. 모델 가중치는 저장소와 설치 패키지에 포함하지 않으며, 모델별 사용 조건은 공급자의 모델 카드에서 확인해야 한다.
