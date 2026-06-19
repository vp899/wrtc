/*
 *  Copyright (c) 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Stream Conductor - manages WebRTC peer connection for stream_client.
 *  Based on the peerconnection/client/conductor but simplified for
 *  headless video-only sending.
 *
 *  Flow:
 *  1. Connect to peerconnection_server with a fixed client name (the stream ID)
 *  2. Wait for a receiver (web browser) to send an offer
 *  3. Set remote description, create answer, add video track
 *  4. Exchange ICE candidates via the signaling server
 */

#ifndef EXAMPLES_PEERCONNECTION_STREAM_CLIENT_STREAM_CONDUCTOR_H_
#define EXAMPLES_PEERCONNECTION_STREAM_CLIENT_STREAM_CONDUCTOR_H_

#include <deque>
#include <memory>
#include <string>

#include "api/data_channel_interface.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "examples/peerconnection/stream_client/peer_connection_client.h"
#include "examples/peerconnection/stream_client/custom_video_source.h"
#include "rtc_base/thread.h"

namespace stream_client {

class StreamConductor : public webrtc::PeerConnectionObserver,
                        public webrtc::CreateSessionDescriptionObserver,
                        public webrtc::DataChannelObserver,
                        public PeerConnectionClientObserver {
 public:
  StreamConductor(const webrtc::Environment& env,
                  PeerConnectionClient* client,
                  const std::string& stream_id,
                  int width,
                  int height,
                  int fps);
  ~StreamConductor() override;

  // Initialize and start streaming.
  bool Initialize();

  // Connect to the signaling server.
  void ConnectToServer(const std::string& server, int port);

  // Set STUN server URI for ICE candidate gathering (e.g. "stun:192.168.96.129:3478")
  void SetStunServer(const std::string& uri);

  // Disconnect and cleanup.
  void Close();

  bool connection_active() const { return peer_connection_ != nullptr; }

  // PeerConnectionObserver
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnAddTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&
          streams) override {}
  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}
  void OnDataChannel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) override;
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override;
  void OnIceConnectionReceivingChange(bool receiving) override {}

  // DataChannelObserver
  void OnStateChange() override {}
  void OnMessage(const webrtc::DataBuffer& buffer) override;

  // PeerConnectionClientObserver
  void OnSignedIn() override;
  void OnDisconnected() override;
  void OnPeerConnected(int id, const std::string& name) override;
  void OnPeerDisconnected(int id) override;
  void OnMessageFromPeer(int peer_id, const std::string& message) override;
  void OnMessageSent(int err) override;
  void OnServerConnectionFailure() override;

  // CreateSessionDescriptionObserver
  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  void OnFailure(webrtc::RTCError error) override;

 private:
  bool CreatePeerConnection();
  void DeletePeerConnection();
  void AddVideoTrack();
  void ScheduleBandwidthStats();
  void SendMessage(const std::string& json_object);
  void ProcessPendingMessages();

  const webrtc::Environment& env_;
  PeerConnectionClient* client_;
  std::string stream_id_;
  int width_;
  int height_;
  int fps_;

  int peer_id_;
  std::string server_;
  std::string stun_server_;  // STUN server URI for ICE gathering

  // Message queue for ICE candidates that arrive before remote description is set
  bool remote_description_set_;
  std::vector<std::string> pending_ice_candidates_;

  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  std::unique_ptr<webrtc::Thread> signaling_thread_;

  webrtc::scoped_refptr<CustomVideoSource> video_source_;
  webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel_;

  std::deque<std::string*> pending_messages_;
  bool stats_running_ = false;
};

}  // namespace stream_client

#endif  // EXAMPLES_PEERCONNECTION_STREAM_CLIENT_STREAM_CONDUCTOR_H_
