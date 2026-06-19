#!/bin/bash
# 弱网模拟 – stream_client 发送端专用
# 包含：namespace 隔离、veth 配置、NAT、弱网损伤、STUN 支持
#
# 用法：
#   sudo ./setup_network.sh          # 创建弱网环境
#   sudo ./setup_network.sh --clean  # 清理环境
#
# 创建后运行 stream_client：
#   sudo ip netns exec ns-sender ./out/demo/stream_client \
#     --server=<HOST_IP> --port=8888 --id=stream_001 \
#     --stun=stun:<HOST_IP>:3478
#
# 浏览器 receiver.html 需配置 STUN：
#   stun:<HOST_IP>:3478

# ========== 可调参数 ==========
BANDWIDTH="1mbit"       # 上行带宽，例如 500kbit, 1mbit
RTT="200ms"             # 双向延迟（我们只限单向，可以取一半）
LOSS="1%"               # 丢包率
# ===============================

NS="ns-sender"
VETH_HOST="veth-host"
VETH_NS="veth-sender"
IP_HOST="10.0.0.1/30"
IP_NS="10.0.0.2/30"
HOST_IP=$(ip route get 8.8.8.8 2>/dev/null | awk '{print $7; exit}')
if [ -z "$HOST_IP" ]; then
  HOST_IP=$(hostname -I | awk '{print $1}')
fi

# ========== 清理模式 ==========
if [ "$1" = "--clean" ]; then
  echo "[clean] 清理 namespace 和 veth..."
  sudo ip netns del $NS 2>/dev/null || true
  sudo ip link del $VETH_HOST 2>/dev/null || true

  echo "[clean] 清理 iptables 规则..."
  # 清理 FORWARD 规则
  sudo iptables -D FORWARD -s 10.0.0.0/30 -j ACCEPT 2>/dev/null || true
  sudo iptables -D FORWARD -d 10.0.0.0/30 -j ACCEPT 2>/dev/null || true
  # 清理 SNAT 规则
  sudo iptables -t nat -D POSTROUTING -s 10.0.0.0/30 -d $HOST_IP -j SNAT --to-source $HOST_IP 2>/dev/null || true
  # 清理 MASQUERADE 规则
  sudo iptables -t nat -D POSTROUTING -s 10.0.0.0/30 -o $(ip route get 8.8.8.8 2>/dev/null | awk '{print $5; exit}') -j MASQUERADE 2>/dev/null || true

  echo "[clean] ✅ 清理完成"
  exit 0
fi

# ========== 创建弱网环境 ==========

# 1. 清理旧环境
echo "[1] 清理旧 namespace 和 veth..."
sudo ip netns del $NS 2>/dev/null || true
sudo ip link del $VETH_HOST 2>/dev/null || true

# 2. 创建 namespace
echo "[2] 创建 namespace: $NS"
sudo ip netns add $NS

# 3. 创建 veth 对，一端在宿主机，另一端放入 namespace
echo "[3] 创建 veth 对..."
sudo ip link add $VETH_HOST type veth peer name $VETH_NS
sudo ip link set $VETH_NS netns $NS

# 4. 配置 IP 地址
echo "[4] 配置 IP..."
sudo ip addr add $IP_HOST dev $VETH_HOST
sudo ip netns exec $NS ip addr add $IP_NS dev $VETH_NS

# 5. 启动网卡
echo "[5] 启动网卡..."
sudo ip link set $VETH_HOST up
sudo ip netns exec $NS ip link set lo up
sudo ip netns exec $NS ip link set $VETH_NS up

# 6. 配置 namespace 内的路由：默认网关指向宿主机的 veth IP
echo "[6] 配置路由..."
sudo ip netns exec $NS ip route add default via 10.0.0.1

# 7. 在宿主机上开启 IP 转发，并设置 NAT
echo "[7] 开启 IP 转发与 NAT..."
sudo sysctl -w net.ipv4.ip_forward=1

# 确保 MASQUERADE 规则存在（使 ns-sender 能访问外网）
PHYS_IF=$(ip route get 8.8.8.8 2>/dev/null | awk '{print $5; exit}')
if [ -n "$PHYS_IF" ]; then
  sudo iptables -t nat -C POSTROUTING -s 10.0.0.0/30 -o $PHYS_IF -j MASQUERADE 2>/dev/null || \
  sudo iptables -t nat -A POSTROUTING -s 10.0.0.0/30 -o $PHYS_IF -j MASQUERADE
fi

# 8. FORWARD 链：放行 namespace 流量
#    （默认 FORWARD 策略可能是 DROP，需要显式放行）
echo "[8] 配置 FORWARD 规则..."
sudo iptables -C FORWARD -s 10.0.0.0/30 -j ACCEPT 2>/dev/null || \
sudo iptables -I FORWARD 1 -s 10.0.0.0/30 -j ACCEPT
sudo iptables -C FORWARD -d 10.0.0.0/30 -j ACCEPT 2>/dev/null || \
sudo iptables -I FORWARD 2 -d 10.0.0.0/30 -j ACCEPT

# 9. SNAT 规则：使 namespace 到宿主机服务的流量源 IP 改写为宿主机 IP
#    （STUN 服务器需要看到宿主机 IP 而不是 namespace IP）
echo "[9] 配置 SNAT 规则（STUN 支持）..."
sudo iptables -t nat -C POSTROUTING -s 10.0.0.0/30 -d $HOST_IP -j SNAT --to-source $HOST_IP 2>/dev/null || \
sudo iptables -t nat -A POSTROUTING -s 10.0.0.0/30 -d $HOST_IP -j SNAT --to-source $HOST_IP

# 10. 在 sender namespace 的 veth 接口上配置弱网损伤（上行）
echo "[10] 配置弱网损伤（带宽=$BANDWIDTH, 丢包=$LOSS）..."
HALF_RTT=$(( $(echo $RTT | sed 's/ms//') / 2 ))

# 添加 tbf 带宽限制
sudo ip netns exec $NS tc qdisc add dev $VETH_NS root handle 1: tbf \
    rate $BANDWIDTH burst 32kbit latency 400ms

# 添加 netem 延迟 + 丢包
sudo ip netns exec $NS tc qdisc add dev $VETH_NS parent 1:1 handle 10: netem \
    delay ${HALF_RTT}ms loss $LOSS

echo ""
echo "============================================"
echo "✅ namespace 弱网环境配置完成！"
echo "============================================"
echo ""
echo "Namespace        : $NS"
echo "NS 内部 veth IP  : 10.0.0.2"
echo "宿主 veth IP     : 10.0.0.1"
echo "宿主物理 IP      : $HOST_IP"
echo ""
echo "弱网参数         : 带宽=$BANDWIDTH, 单向延迟=${HALF_RTT}ms, 丢包=$LOSS"
echo ""
echo "NAT 规则         :"
echo "  - MASQUERADE   : namespace → 外网"
echo "  - SNAT         : namespace → 宿主机服务（STUN 支持）"
echo "  - FORWARD      : 放行 namespace 流量"
echo ""
echo "============================================"
echo "启动 stream_client（带 STUN）："
echo "  sudo ip netns exec $NS ./out/demo/stream_client \\"
echo "    --server=$HOST_IP --port=8888 --id=stream_001 \\"
echo "    --stun=stun:$HOST_IP:3478"
echo ""
echo "启动 STUN 服务器（coturn）："
echo "  ./bin/turnserver --listening-ip=$HOST_IP --listening-port=3478 \\"
echo "    --no-tls --no-dtls --stun-only --cli-password=test123"
echo ""
echo "浏览器 receiver.html STUN 配置："
echo "  stun:$HOST_IP:3478"
echo "============================================"
