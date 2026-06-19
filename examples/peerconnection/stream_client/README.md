# stream_client - WebRTC 视频流发送端

## 概述

`stream_client` 是一个无头 (headless) 的 WebRTC 视频发送端程序，它：

1. **生成 YUV420p 动画帧**（320×240 @ 30fps）：
   - 左上角显示时间戳（格式：`HH:MM:SS:mmm`，从 `00:00:00:000` 开始）
   - 一个 100×100 的绿色矩形，水平方向每帧移动 1 像素，碰到边缘反弹

2. **使用 WebRTC 内置的 OpenH264 编码器** 将每一帧编码为 H264

3. **通过 `peerconnection_server` 信令服务器** 与接收端（Chrome 浏览器）交换 SDP 和 ICE 候选

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    VMware Ubuntu                             │
│                                                             │
│  ┌──────────────────┐    ┌──────────────────────────────┐   │
│  │ peerconnection   │    │        stream_client          │   │
│  │     _server      │◄───│                              │   │
│  │   (port 8888)    │    │  FrameGenerator (320x240)    │   │
│  │                  │    │       ↓                      │   │
│  │  信令服务器       │    │  CustomVideoSource (30fps)   │   │
│  │                  │    │       ↓                      │   │
│  │  - sign_in       │    │  WebRTC PeerConnection       │   │
│  │  - wait          │    │       ↓                      │   │
│  │  - message       │    │  OpenH264 Encoder            │   │
│  └──────────────────┘    │       ↓                      │   │
│         ▲                │  RTP → 发送到接收端            │   │
│         │                └──────────────────────────────┘   │
│         │ 信令 (HTTP)                                       │
└─────────┼───────────────────────────────────────────────────┘
          │
          │  网络
          │
┌─────────┼───────────────────────────────────────────────────┐
│         │           Windows 主机                             │
│         ▼                                                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Chrome 浏览器                            │   │
│  │                                                      │   │
│  │  receiver.html                                       │   │
│  │  - 连接信令服务器                                      │   │
│  │  - 发送 SDP Offer                                    │   │
│  │  - 接收 H264 视频流                                   │   │
│  │  - 播放动画视频                                       │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## 前置条件

### Ubuntu (VMware)
- 已编译的 WebRTC 项目（包含 `peerconnection_server`）
- OpenH264 库（WebRTC 自带）
- GCC/G++ 编译器

### Windows 主机
- Chrome 浏览器（版本 72+，支持 WebRTC Unified Plan）

## 编译

### 方法 1：在 WebRTC 源码树中编译（推荐）

1. 将 `stream_client` 目录复制到 WebRTC 源码树的 `examples/peerconnection/` 下：
   ```bash
   cp -r stream_client/ /path/to/webrtc/src/examples/peerconnection/
   ```

2. 将本项目附带的 `examples/BUILD.gn` 替换到 WebRTC 源码树（或手动将 `stream_client` target 合并进去）：
   ```bash
   cp examples/BUILD.gn /path/to/webrtc/src/examples/BUILD.gn
   ```

3. 使用 GN 生成构建文件：
   ```bash
   cd /path/to/webrtc/src
   gn gen out/Release --args='is_debug=false rtc_use_h264=true'
   ```

4. 编译：
   ```bash
   ninja -C out/Release examples:stream_client
   ```

### 方法 2：独立编译（CMake）

如果不想在 WebRTC 源码树中编译，可以使用 CMake：

```bash
cd examples/peerconnection/stream_client
mkdir build && cd build
cmake .. -DWEBRTC_ROOT=/path/to/webrtc/src
make -j$(nproc)
```

## 部署和运行

### 步骤 1：在 Ubuntu 上启动信令服务器

```bash
# 进入 WebRTC 编译输出目录
cd /path/to/webrtc/src/out/Release

# 启动 peerconnection_server，监听 8888 端口
./peerconnection_server --port=8888
```

你会看到：
```
Server listening on port 8888
```

### 步骤 2：在 Ubuntu 上启动 stream_client

```bash
# 在另一个终端
cd /path/to/webrtc/src/out/Release

# 启动 stream_client
./stream_client --server=127.0.0.1 --port=8888 --id=stream_001
```

**参数说明：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--server` | `localhost` | 信令服务器地址 |
| `--port` | `8888` | 信令服务器端口 |
| `--id` | `stream_001` | 流 ID（接收端需要用这个 ID 连接）|
| `--width` | `320` | 视频宽度 |
| `--height` | `240` | 视频高度 |
| `--fps` | `30` | 帧率 |

你会看到：
```
=== stream_client ===
Server:     127.0.0.1:8888
Stream ID:  stream_001
Resolution: 320x240 @ 30fps
Codec:      H264 (OpenH264)

Press Ctrl+C to stop.

Connecting to signaling server 127.0.0.1:8888...
Signed in to server as 'stream_001'
Waiting for a receiver to connect...
```

### 步骤 3：在 Windows Chrome 中打开接收页面

**方法 A：直接打开 HTML 文件**

1. 将 `receiver.html` 复制到 Windows 主机
2. 用 Chrome 打开 `receiver.html`
3. 在 "Server" 输入框填入：`http://<Ubuntu_IP>:8888`（例如 `http://192.168.1.100:8888`）
4. 在 "Stream ID" 输入框填入：`stream_001`（必须与 `--id` 参数一致）
5. 点击 "Connect"

**方法 B：通过 URL 参数自动连接**

在 Chrome 地址栏输入：
```
file:///path/to/receiver.html?server=http://192.168.1.100:8888&id=stream_001&auto=1
```

**方法 C：在 Ubuntu 上托管 HTML（可选）**

可以使用 Python 简单 HTTP 服务器托管：
```bash
cd /path/to/stream_client
python3 -m http.server 8080
```

然后在 Chrome 中访问：
```
http://<Ubuntu_IP>:8080/receiver.html?server=http://<Ubuntu_IP>:8888&id=stream_001&auto=1
```

### 步骤 4：观看视频

连接成功后，Chrome 浏览器中会显示：
- 黑色背景上
- 左上角：递增的时间戳（白色）
- 中间：一个水平弹跳的绿色矩形

## 信令流程详解

```
stream_client          peerconnection_server        Chrome (receiver)
     │                         │                          │
     │  GET /sign_in?stream_001│                          │
     │────────────────────────►│                          │
     │  200 OK (peer_id=1)     │                          │
     │◄────────────────────────│                          │
     │                         │                          │
     │  GET /wait?peer_id=1    │                          │
     │────────────────────────►│ (长轮询等待)              │
     │                         │                          │
     │                         │  GET /sign_in?receiver_2 │
     │                         │◄─────────────────────────│
     │                         │  200 OK (peer_id=2)      │
     │                         │─────────────────────────►│
     │                         │                          │
     │  200 OK (通知新peer)    │                          │
     │◄────────────────────────│                          │
     │                         │                          │
     │                         │  POST /message (Offer)   │
     │                         │◄─────────────────────────│
     │  200 OK (转发Offer)     │                          │
     │◄────────────────────────│                          │
     │                         │                          │
     │  [创建 PeerConnection]  │                          │
     │  [添加视频轨道]          │                          │
     │  [创建 Answer]          │                          │
     │                         │                          │
     │  POST /message (Answer) │                          │
     │────────────────────────►│                          │
     │  200 OK                 │  200 OK (转发Answer)     │
     │◄────────────────────────│─────────────────────────►│
     │                         │                          │
     │  [交换 ICE candidates]  │                          │
     │◄───────────────────────►│◄────────────────────────►│
     │                         │                          │
     │  [H264 RTP 视频流]      │                          │
     │────────────────────────────────────────────────────►│
     │                         │                          │
     │                         │     [Chrome 播放视频]     │
```

## 关键源码说明

### frame_generator.h/cc
- `FrameGenerator` 类：生成 320×240 YUV420p 帧
- `SimpleFont` 类：5×7 位图字体，用于渲染时间戳
- 每帧包含：深灰色背景、白色时间戳、绿色弹跳矩形

### custom_video_source.h/cc
- `CustomVideoSource` 类：继承 `webrtc::VideoTrackSource`
- 在独立线程中以 30fps 生成帧
- 实现 `AddOrUpdateSink`/`RemoveSink` 接口

### stream_conductor.h/cc
- `StreamConductor` 类：管理 WebRTC PeerConnection
- 处理信令：SDP Offer/Answer 交换、ICE 候选交换
- 使用 OpenH264 作为首选编码器
- 作为 answerer（等待接收端发送 offer）

### receiver.html
- 纯 HTML/JavaScript 实现
- 通过 XMLHttpRequest 与 `peerconnection_server` 通信
- 创建 `RTCPeerConnection`，发送 offer，接收 answer
- 接收 H264 视频流并在 `<video>` 标签中播放

## 故障排除

### 1. "Failed to create PeerConnectionFactory"
- 确保 WebRTC 编译时启用了 H264：`rtc_use_h264=true`
- 检查 OpenH264 库是否正确链接

### 2. Chrome 无法连接
- 确保 Ubuntu 防火墙允许 8888 端口
- 确保使用正确的 Ubuntu IP 地址
- 检查 Chrome 控制台（F12）是否有错误信息

### 3. 视频无法播放
- 检查 `stream_client` 日志是否有 "Video track added" 消息
- 确保 Chrome 版本支持 H264 解码
- 尝试在 Chrome 中访问 `chrome://webrtc-internals` 查看详细信息

### 4. ICE 连接失败
- 如果 Ubuntu 和 Windows 在不同子网，可能需要配置 STUN/TURN 服务器
- 在 `stream_conductor.cc` 中添加 ICE 服务器配置

### 5. Ubuntu 和 Windows 网络不通
- 确保 VMware 网络设置为桥接模式（Bridged）或 NAT 端口转发
- 在 Ubuntu 上运行 `ip addr` 查看 IP 地址
- 在 Windows 上 `ping <Ubuntu_IP>` 测试连通性

## 高级配置

### 使用 STUN/TURN 服务器

如果需要 NAT 穿透，在 `stream_conductor.cc` 中添加：

```cpp
webrtc::PeerConnectionInterface::IceServer stun_server;
stun_server.uri = "stun:stun.l.google.com:19302";
config.servers.push_back(stun_server);
```

在 `receiver.html` 中修改：
```javascript
var iceServers = [
  { urls: 'stun:stun.l.google.com:19302' }
];
```

### 修改视频参数

```bash
# 640x480 @ 24fps
./stream_client --server=127.0.0.1 --port=8888 --id=stream_001 \
                --width=640 --height=480 --fps=24
```

## 许可证

BSD-style license，与 WebRTC 项目一致。
