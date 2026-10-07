# Jetson Traffic CCTV Vision

Jetson Nano에서 UTIC CCTV 영상을 실시간 처리하고 차량 탐지 결과를 Final Server로 전송하는 C++17 Vision Client입니다.

```text
UTIC CCTV
→ HLS / OpenCV FFMPEG
→ Letterbox
→ TensorRT YOLO26n FP16
→ NMS
→ Tracker / Display
→ 1초 Snapshot
→ Final Server TCP
```

## Build

필요 환경:

- OpenCV 4.8.0 + FFMPEG
- CUDA / TensorRT
- libcurl
- nlohmann/json

```bash
cmake -S . -B build-cctv \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON

cmake --build build-cctv -j2

(cd build-cctv && ctest --output-on-failure)
```

## Run

UTIC 인증키는 환경변수로 전달합니다.

```bash
read -r -s -p 'UTIC API key: ' UTIC_API_KEY
printf '\n'
export UTIC_API_KEY

./run <server_ip> <server_port>
```

키 입력, `export`, 실행은 동일한 터미널에서 수행합니다.

개발 바이너리를 직접 실행하려면:

```bash
DISPLAY=:1 XAUTHORITY=/home/jetson/.Xauthority \
  ./build-cctv/bin/edge_vision <server_ip> <server_port>
```

키 값을 출력하지 않고 현재 환경에서 설정 여부를 확인하려면:

```bash
python3 -c 'import os; v=os.getenv("UTIC_API_KEY"); print("UTIC_API_KEY:", "absent" if v is None else "empty" if not v else "nonempty")'
```

실행 후 필요하면 환경변수를 제거합니다.

```bash
unset UTIC_API_KEY
```

## CCTV 선택

CCTV는 `UTIC_CCTV_ID` 환경변수로 선택합니다.

```bash
export UTIC_CCTV_ID='<CCTV_ID>'
./run <server_ip> <server_port>
```

UTIC 개방데이터 목록에서 CCTV ID를 검색할 수 있습니다.

```bash
curl -sS \
  "http://www.utic.go.kr/guide/cctvOpenData.do?key=${UTIC_API_KEY}" \
  -o /tmp/utic_open.html

grep -n -C 3 '<CCTV_NAME>' /tmp/utic_open.html
```

프로그램 시작 시 UTIC 개방데이터를 조회하고, 동일한 HTTP session/cookie를 사용하여 CCTV metadata와 HLS 주소를 조회합니다.

실제 HLS `.m3u8` 주소는 Runtime에 조회하여 사용합니다.

## Network Data

TCP 메시지는 `4-byte big-endian length + UTF-8 JSON` 형식입니다.

1초마다 최신 처리 Frame을 기준으로 다음 메시지를 각각 전송합니다.

- `vision`: 전체 Detection 배열
- `vehicle_count`: NMS 이후 차량 수

두 메시지는 같은 `frame_id`와 `timestamp_ms`를 사용하며, 각각 별도의 `message_id`를 가집니다.

`track_id`는 내부 Tracking / Display 용도로 사용합니다.

## PAUSE / RESUME

`PAUSED` 상태에서도 Vision Pipeline은 계속 동작합니다.

```text
CCTV Frame
→ TensorRT Inference
→ NMS
→ Tracking
→ Display
```

Network Snapshot 생성과 전송은 일시 중단되며, `RESUME` 이후 새로운 Frame을 기준으로 Snapshot 생성과 전송을 재개합니다.

## Deployment

운영 설치 경로:

```text
/opt/traffic_cctv_vision/bin/edge_vision
/opt/traffic_cctv_vision/models/yolo26n_fp16.engine
/var/lib/traffic_cctv_vision/boot_id.dat
```

재설치:

```bash
cmake -S . -B build-deploy \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/traffic_cctv_vision \
  -DMODEL_PATH=/opt/traffic_cctv_vision/models/yolo26n_fp16.engine \
  -DBOOT_ID_PATH=/var/lib/traffic_cctv_vision/boot_id.dat

cmake --build build-deploy -j2

sudo cmake --install build-deploy
```

## Validation

자동 테스트는 다음 항목을 검증합니다.

```text
Snapshot
UTIC 환경변수 설정 및 프로세스 상속
UTIC CCTV 선택
HLS URL 추출
HTML 주석 내부 HLS 후보 제외
HLS query parameter 보존
Network Integration
```

실제 UTIC CCTV 입력부터 TensorRT 추론, Tracking, Snapshot 전송까지 Jetson Nano에서 통합 검증했습니다.

```text
UTIC CCTV 조회                         PASS
CCTV metadata 조회                     PASS
HLS URL Runtime 추출                   PASS
OpenCV FFMPEG Frame Capture            PASS
1280x720 CCTV Frame                    PASS
TensorRT YOLO26n FP16 Inference        PASS
Vehicle Detection / NMS                PASS
Tracking / Display                     PASS
1초 Snapshot                           PASS
PAUSE / RESUME                         PASS
SIGINT Graceful Shutdown               PASS
```

측정된 Vision 처리 성능은 약 `10.5~11 FPS`, TensorRT 추론 시간은 약 `55 ms`였습니다.

UTIC Stream page의 HTML 주석 내부에도 `.m3u8` 문자열이 포함될 수 있어, HLS parser에서 주석 영역을 제외하고 실제 Stream URL을 선택하도록 처리했습니다.
시작 조회의 일시적 네트워크 실패도 30초 간격으로 실행당 최대 4회 시도합니다. 오류 로그는 open-data / metadata / playback-page 단계와 curl 원인을 구분하며 키·전체 URL은 출력하지 않습니다. 대기 중 Ctrl+C로 종료할 수 있습니다.
