# iiAudioGen 0.3.0 검증 기록

## 2026-10-27 ONNX model boundary

On macOS arm64, the `build/safetensors-policy/` C++ build and all eight CTest cases passed. A recording backend confirms case-insensitive ONNX rejection and forwarding of GGUF, safetensors and other paths. A real directory named with an `.onnx` suffix is treated as a directory. This contract test does not prove GGUF audio inference. The default Diffusers backend continues to require a local model directory, safetensors weights and Python.

검증일은 2026-10-03이며 환경은 macOS arm64, Apple M1 Max, 32GiB 메모리이다. C++ 빌드는 Apple Clang 21.0.0, CMake 4.4.3, Ninja를 사용했다. 실제 모델 연산은 CPU에서 수행했다.

## 빌드·계약·설치 소비자

공유 라이브러리와 정적 라이브러리 구성이 모두 빌드되었다. 각 구성의 CTest 7개와 설치 패키지만 참조하는 소비자 테스트 1개가 통과했다. 전체는 16개 CTest 실행이다. Python 워커 테스트 항목 내부에서는 별도의 여러 사례를 검증하므로 실행 수와 내부 사례 수를 혼동하지 않는다.

일반 테스트는 요청·모델 입력·오류 계약, 실제 모델 미로딩 상태, 로컬 전용 안전 가중치 로딩 설정, 사용자 컴포넌트 거절, 실패한 모델 교체의 기존 상태 유지, 이진 응답 분할, 비유한 PCM, 과도한 응답 길이, 프로세스 종료, 시간 초과와 재시작을 검증한다. 프로토콜 테스트의 고정 PCM은 모델 추론의 증거로 사용하지 않는다.

설치 소비자는 `find_package(iiAudioGen 0.3 CONFIG REQUIRED)`와 설치된 `<iiAudioGen/Diffusers.h>`를 사용하며 공개 백엔드 팩터리와 생성 API를 호출했다. 검증용 설치 위치는 `build/stage`와 `build/static/stage`이다.

## 실제 모델과 런타임

전용 Python 3.12.14 환경에 `runtime/requirements.txt`를 설치했으며 `pip check`가 성공했다. 주요 버전은 PyTorch 2.13.0, Diffusers 0.35.2, Transformers 4.48.3, Hugging Face Hub 0.36.0, NumPy 2.5.2, SciPy 1.18.1이다. 기존 iiLocalDiffusion 환경은 변경하지 않았다.

| 모델 | 공급자 리비전 | 검증 |
| --- | --- | --- |
| `cvssp/audioldm2` | `c8e7e189d324425c05c4c2f81214041ef4107983` | 실제 로딩, 같은 런타임에서 3회 생성, 시드 재현성, 프롬프트에 따른 PCM 차이, 설치된 CLI의 스테레오 출력 |
| `cvssp/audioldm-s-full-v2` | `feeb3d14203495a4b6ac0893cbdedb2159b4819c` | 설치된 CLI로 실제 로딩·생성·16kHz WAV 저장 |

각 모델 디렉터리의 다운로드 메타데이터는 모델별로 동일한 리비전이었다. AudioLDM2는 26개 파일, AudioLDM 소형 모델은 15개 파일이다. 가중치와 생성 결과는 `build/` 아래에 있으며 Git 저장소와 SDK 설치 패키지에 포함하지 않는다.

AudioLDM2 수락 테스트는 CPU, 20단계, 3초, 44.1kHz 모노, 시드 42로 진행했다. 첫 프롬프트는 `Gentle rain falling on leaves in a quiet forest.`이며 이를 두 번 실행했다. 세 번째 프롬프트는 `A church bell ringing repeatedly in the distance.`이다. 동일 프롬프트·동일 시드의 전체 float PCM이 정확히 일치했고, 다른 프롬프트의 PCM은 달랐다. 출력은 각각 132,300프레임이었다. 세 번의 생성 시간은 약 44.37초이며 모델 로딩 시간은 이 측정에 포함하지 않았다.

## 결과 파일

| 파일 | 형식 | SHA-256 |
| --- | --- | --- |
| `build/real-audioldm2/rain.wav` | 3초, 44.1kHz, 모노, PCM16 | `6ce78584eee4ae193b3dc368ed79e017e32a541a127299366de14a882dd69989` |
| `build/real-audioldm2/bells.wav` | 3초, 44.1kHz, 모노, PCM16 | `68da91bc8158781ef73381d86c959b9edc7aad4e9753d5f7e7d0b5752076d3c6` |
| `build/real-audioldm2/installed-stereo.wav` | 2초, 48kHz, 2채널, PCM16 | `5e316f55988b1c98c21f37dc94411a82f32404cd803eb353262e5cade760014f` |
| `build/real-audioldm/installed.wav` | 1초, 16kHz, 모노, PCM16 | `2788d6fa6782015172a190f75941ebabda77c80d4351fea75c67821cdea566df` |
| `build/real-audioldm2/final.wav` | 최종 설치본, 1초, 16kHz, 모노, PCM16 | `26c2c27b0b99e7cca61320e9813e1e0609238fa05a6d9d50aef89e847b1d324f` |

WAV 헤더와 프레임 수는 Python 표준 `wave`로 독립적으로 읽어 검증했다. C++ 백엔드는 양수 프레임 수, 요청 형식 일치와 모든 float PCM 샘플의 유한성을 확인한다. AudioLDM2 빗소리 WAV의 RMS는 약 0.015694, 종소리 WAV의 RMS는 약 0.137071이었다. 모노 생성 모델의 2채널 출력은 양쪽 채널을 복제하는 형식 변환이며 공간 스테레오 생성 증거가 아니다.

최종 모델 구성 검증은 공식 AudioLDM2의 `audioldm2` 내부 컴포넌트를 허용하고 사용자 Python 컴포넌트와 로컬 Python 덮어쓰기를 거절한다. 이 변경 후 두 빌드 구성과 설치 소비자의 16개 CTest를 다시 통과했으며, 최종 설치 CLI에서 AudioLDM2를 로딩하고 `final.wav`의 16,000프레임을 실제 생성했다.

## 사용 조건과 검증 한계

검증 모델의 모델 카드에는 CC-BY-NC-SA-4.0이 선언되어 있다. 이는 개발용 참고 모델이며 상용 작업에 사용할 모델은 해당 공급자의 조건에 맞게 선택해야 한다. [AudioLDM2 모델 카드](https://huggingface.co/cvssp/audioldm2), [AudioLDM 모델 카드](https://huggingface.co/cvssp/audioldm-s-full-v2)가 근거이다.

검증한 범위는 실제 프롬프트 조건화, 가중치 로딩, 파형 생성, 형식 변환, 설치된 C++ SDK 경로와 결과 파일이다. 주관적인 음질, 프롬프트 의미 일치도, 다른 체크포인트, Windows·Linux 실행, MPS·CUDA와 Neural Engine 실행은 검증하지 않았다.
