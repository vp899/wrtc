# stream_client — WebRTC 弱网视频推流工具

基于 WebRTC 的无头视频发送端，生成动画 YUV420 帧并通过 H264 编码推流到浏览器接收端。支持弱网模拟环境下的 STUN 穿透。

## 功能

- 自动生成 320x240 @ 30fps 的 YUV420 动画帧
- 每帧叠加时间戳（HH:MM:SS:mmm 格式）
- 100x100 绿色矩形反弹动画
- WebRTC 内置 OpenH264 编码
- 通过 peerconnection_server 进行信令
- 浏览器端实时显示延迟、带宽、帧率等指标

## 文件说明

```
stream_client/
├── main.cc                  # 入口，命令行参数解析
├── stream_conductor.cc      # WebRTC 核心逻辑（视频工厂、PeerConnection、ICE）
├── stream_conductor.h       # 头文件
├── custom_video_source.cc   # 自定义视频源（YUV 帧生成）
├── custom_video_source.h
├── frame_generator.cc       # 动画帧生成器（时间戳 + 弹跳矩形）
├── frame_generator.h
├── peer_connection_client.cc # 信令客户端（与 peerconnection_server 通信）
├── peer_connection_client.h
├── defaults.cc / .h         # 默认配置
└── receiver.html            # 浏览器接收端（含延迟/带宽叠加层）
```

## 编译

```bash
cd ~/rtc/buildwebrtc/src
ninja -C out/demo stream_client
```

## 运行

### 1. 启动信令服务器

```bash
./out/demo/peerconnection_server
```

### 2. 启动 STUN 服务器（弱网 namespace 环境必需）

```bash
./bin/turnserver --listening-ip=<HOST_IP> --listening-port=3478 \
  --no-tls --no-dtls --stun-only --cli-password=test123
```

### 3. 创建弱网环境（可选）

```bash
sudo ./setup_network.sh
```

脚本自动配置：
- network namespace（ns-sender）
- veth pair + NAT
- FORWARD/SNAT 规则（STUN 支持）
- tc 弱网损伤（带宽/延迟/丢包）

清理环境：`sudo ./setup_network.sh --clean`

### 4. 启动 stream_client

**直接运行（无 namespace）：**

```bash
./out/demo/stream_client --server=<HOST_IP> --port=8888 --id=stream_001
```

**在 namespace 中运行（弱网环境）：**

```bash
sudo ip netns exec ns-sender ./out/demo/stream_client \
  --server=<HOST_IP> --port=8888 --id=stream_001 \
  --stun=stun:<HOST_IP>:3478
```

### 5. 浏览器接收端

用 Chrome 打开 `receiver.html`，配置：
- Server: `http://<HOST_IP>:8888`
- Stream ID: `stream_001`
- STUN: `stun:<HOST_IP>:3478`（弱网环境必需）

点击 Connect 即可接收视频流。

## 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server` | localhost | peerconnection_server 地址 |
| `--port` | 8888 | 信令服务器端口 |
| `--id` | stream_001 | 流 ID（固定标识） |
| `--stun` | （空） | STUN 服务器 URI，如 `stun:192.168.1.100:3478` |
| `--width` | 320 | 视频宽度 |
| `--height` | 240 | 视频高度 |
| `--fps` | 30 | 帧率 |

## 浏览器端叠加层指标

| 指标 | 说明 |
|------|------|
| RTT | WebRTC RTCP 报告的往返延迟 |
| One-way delay | RTT / 2 单向延迟估算 |
| Avg(N) | 过去 N 个采样的平均单向延迟 |
| Min / Max | 滑动窗口内的最小/最大延迟 |
| Actual BW | 实际接收带宽（从 bytesReceived 计算） |
| Avail BW | WebRTC 估算的可用带宽 |
| Frames | 已解码帧数 |
| FPS | 当前帧率 |

## stream_client 终端日志

每 2 秒输出带宽统计：

```
[stream_client] BW | avail: 1200 kbps | RTT: 120 ms | bytes_sent: 123456
```

## 弱网参数调整

编辑 `setup_network.sh` 顶部的可调参数：

```bash
BANDWIDTH="1mbit"       # 上行带宽
RTT="200ms"             # 双向延迟
LOSS="0%"               # 丢包率
```

修改后重新运行 `sudo ./setup_network.sh` 生效。

## 技术要点

### 纯视频架构

使用自定义 `VideoOnlyMediaFactory` 替代 `EnableMedia()`，彻底绕开音频：
- 不创建 WebRtcVoiceEngine
- 不初始化 AudioDeviceModule（ADM）
- 可在 sudo / network namespace 下正常运行

### STUN 穿透

namespace 内的 stream_client（IP: 10.0.0.2）无法被浏览器直接访问。通过 STUN 服务器发现外部 IP（宿主机 IP），生成 srflx ICE candidate，使浏览器能够建立连接。

### 网络架构

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────┐
│  Browser    │────▶│  peerconnection  │◀────│ stream_client│
│ (Windows)   │     │  server          │     │ (namespace)  │
│             │     │  192.168.96.129  │     │ 10.0.0.2     │
│             │     │  :8888           │     │              │
│             │     └──────────────────┘     │              │
│             │                               │              │
│             │◀──── STUN (srflx) ───────────▶│              │
│             │     192.168.96.129             │              │
│             │                               │              │
│             │◀═══ RTP/DTLS (media) ════════▶│              │
└─────────────┘                               └─────────────┘
```
