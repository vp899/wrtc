/*
 *  Copyright (c) 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Stream Conductor implementation.
 *  Video-only sender: no audio engine, no ADM, no audio codecs.
 */

#include "examples/peerconnection/stream_client/stream_conductor.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "call/call.h"
#include "call/call_config.h"
#include "media/base/media_engine.h"
#include "media/engine/webrtc_video_engine.h"
#include "pc/media_factory.h"
#include "json/reader.h"
#include "json/value.h"
#include "json/writer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/json.h"

namespace stream_client {

namespace {
const char kCandidateSdpMidName[] = "sdpMid";
const char kCandidateSdpMlineIndexName[] = "sdpMLineIndex";
const char kCandidateSdpName[] = "candidate";
const char kSessionDescriptionTypeName[] = "type";
const char kSessionDescriptionSdpName[] = "sdp";
const char kStreamId[] = "stream_client_video";
const char kVideoLabel[] = "stream_video_track";

// ====================================================================
// Dummy voice engine — implements VoiceEngineInterface with no-ops.
// This avoids the null pointer dereference in CompositeMediaEngine
// which unconditionally calls voice().Init().
// ====================================================================
class DummyVoiceEngine : public webrtc::VoiceEngineInterface {
 public:
  DummyVoiceEngine(
      webrtc::scoped_refptr<webrtc::AudioEncoderFactory> encoder_factory,
      webrtc::scoped_refptr<webrtc::AudioDecoderFactory> decoder_factory)
      : encoder_factory_(std::move(encoder_factory)),
        decoder_factory_(std::move(decoder_factory)) {}
  void Init() override {}
  void Terminate() override {}
  void ApplyGlobalOptions(const webrtc::AudioOptions&) override {}
  webrtc::scoped_refptr<webrtc::AudioState> GetAudioState() const override {
    return nullptr;
  }

  std::unique_ptr<webrtc::VoiceMediaSendChannelInterface> CreateSendChannel(
      const webrtc::Environment&, webrtc::Call*,
      const webrtc::MediaConfig&, const webrtc::AudioOptions&,
      const webrtc::CryptoOptions&,
      absl::AnyInvocable<void()>) override { return nullptr; }

  std::unique_ptr<webrtc::VoiceMediaReceiveChannelInterface>
  CreateReceiveChannel(const webrtc::Environment&, webrtc::Call*,
                       const webrtc::MediaConfig&,
                       const webrtc::AudioOptions&,
                       const webrtc::CryptoOptions&) override { return nullptr; }

  std::vector<webrtc::RtpHeaderExtensionCapability> GetRtpHeaderExtensions(
      const webrtc::FieldTrialsView*) const override { return {}; }

  const std::vector<webrtc::Codec>& LegacySendCodecs() const override {
    static const std::vector<webrtc::Codec> empty;
    return empty;
  }
  const std::vector<webrtc::Codec>& LegacyRecvCodecs() const override {
    static const std::vector<webrtc::Codec> empty;
    return empty;
  }

  const webrtc::scoped_refptr<webrtc::AudioEncoderFactory>&
  encoder_factory() const override {
    return encoder_factory_;
  }
  const webrtc::scoped_refptr<webrtc::AudioDecoderFactory>&
  decoder_factory() const override {
    return decoder_factory_;
  }

  bool StartAecDump(webrtc::FileWrapper, int64_t) override { return false; }
  void StopAecDump() override {}
  std::optional<webrtc::AudioDeviceModule::Stats> GetAudioDeviceStats() override {
    return std::nullopt;
  }

 private:
  webrtc::scoped_refptr<webrtc::AudioEncoderFactory> encoder_factory_;
  webrtc::scoped_refptr<webrtc::AudioDecoderFactory> decoder_factory_;
};

// ====================================================================
// Video-only MediaFactory — creates a CompositeMediaEngine with
// DummyVoiceEngine + WebRtcVideoEngine.
// ====================================================================
class VideoOnlyMediaFactory : public webrtc::MediaFactory {
 public:
  VideoOnlyMediaFactory(
      std::unique_ptr<webrtc::VideoEncoderFactory> video_encoder_factory,
      std::unique_ptr<webrtc::VideoDecoderFactory> video_decoder_factory,
      webrtc::scoped_refptr<webrtc::AudioEncoderFactory> audio_encoder_factory,
      webrtc::scoped_refptr<webrtc::AudioDecoderFactory> audio_decoder_factory)
      : video_encoder_factory_(std::move(video_encoder_factory)),
        video_decoder_factory_(std::move(video_decoder_factory)),
        audio_encoder_factory_(std::move(audio_encoder_factory)),
        audio_decoder_factory_(std::move(audio_decoder_factory)) {}

  std::unique_ptr<webrtc::Call> CreateCall(
      webrtc::CallConfig config) override {
    return webrtc::Call::Create(std::move(config));
  }

  std::unique_ptr<webrtc::MediaEngineInterface> CreateMediaEngine(
      const webrtc::Environment& env,
      webrtc::PeerConnectionFactoryDependencies& /*deps*/) override {
    auto voice_engine = std::make_unique<DummyVoiceEngine>(
        audio_encoder_factory_, audio_decoder_factory_);
    auto video_engine = std::make_unique<webrtc::WebRtcVideoEngine>(
        std::move(video_encoder_factory_),
        std::move(video_decoder_factory_),
        env.field_trials());
    return std::make_unique<webrtc::CompositeMediaEngine>(
        std::move(voice_engine), std::move(video_engine));
  }

 private:
  std::unique_ptr<webrtc::VideoEncoderFactory> video_encoder_factory_;
  std::unique_ptr<webrtc::VideoDecoderFactory> video_decoder_factory_;
  webrtc::scoped_refptr<webrtc::AudioEncoderFactory> audio_encoder_factory_;
  webrtc::scoped_refptr<webrtc::AudioDecoderFactory> audio_decoder_factory_;
};

}  // namespace

class DummySetSessionDescriptionObserver
    : public webrtc::SetSessionDescriptionObserver {
 public:
  static webrtc::scoped_refptr<DummySetSessionDescriptionObserver> Create() {
    return webrtc::make_ref_counted<DummySetSessionDescriptionObserver>();
  }
  void OnSuccess() override { RTC_LOG(LS_INFO) << "SetSessionDescription OK"; }
  void OnFailure(webrtc::RTCError error) override {
    RTC_LOG(LS_ERROR) << "SetSessionDescription failed: " << error.message();
  }
};

StreamConductor::StreamConductor(const webrtc::Environment& env,
                                 PeerConnectionClient* client,
                                 const std::string& stream_id,
                                 int width,
                                 int height,
                                 int fps)
    : env_(env),
      client_(client),
      stream_id_(stream_id),
      width_(width),
      height_(height),
      fps_(fps),
      peer_id_(-1),
      remote_description_set_(false) {
  client_->RegisterObserver(this);
}

StreamConductor::~StreamConductor() {
  RTC_DCHECK(!peer_connection_);
}

bool StreamConductor::Initialize() {
  RTC_LOG(LS_INFO) << "Initializing StreamConductor";

  // Only create signaling thread once
  if (!signaling_thread_) {
    signaling_thread_ = webrtc::Thread::CreateWithSocketServer();
    signaling_thread_->Start();
  }

  // Create PeerConnectionFactory — video-only, no real audio.
  webrtc::PeerConnectionFactoryDependencies deps;
  deps.signaling_thread = signaling_thread_.get();
  deps.env = env_;

  // No audio factories, no ADM.
  deps.audio_encoder_factory = nullptr;
  deps.audio_decoder_factory = nullptr;
  deps.adm = nullptr;
  deps.video_encoder_factory = nullptr;
  deps.video_decoder_factory = nullptr;

  // Use our video-only media factory instead of EnableMedia().
  // This completely skips real audio/ADM initialization.
  deps.media_factory = std::make_unique<VideoOnlyMediaFactory>(
      std::make_unique<webrtc::VideoEncoderFactoryTemplate<
          webrtc::OpenH264EncoderTemplateAdapter,
          webrtc::LibvpxVp8EncoderTemplateAdapter,
          webrtc::LibvpxVp9EncoderTemplateAdapter,
          webrtc::LibaomAv1EncoderTemplateAdapter>>(),
      std::make_unique<webrtc::VideoDecoderFactoryTemplate<
          webrtc::OpenH264DecoderTemplateAdapter,
          webrtc::LibvpxVp8DecoderTemplateAdapter,
          webrtc::LibvpxVp9DecoderTemplateAdapter,
          webrtc::Dav1dDecoderTemplateAdapter>>(),
      webrtc::CreateBuiltinAudioEncoderFactory(),
      webrtc::CreateBuiltinAudioDecoderFactory());

  peer_connection_factory_ =
      webrtc::CreateModularPeerConnectionFactory(std::move(deps));

  if (!peer_connection_factory_) {
    RTC_LOG(LS_ERROR) << "Failed to create PeerConnectionFactory";
    return false;
  }

  RTC_LOG(LS_INFO) << "StreamConductor initialized successfully (video-only)";
  return true;
}

void StreamConductor::SetStunServer(const std::string& uri) {
  stun_server_ = uri;
}

void StreamConductor::ConnectToServer(const std::string& server, int port) {
  server_ = server;
  client_->Connect(server, port, stream_id_);
}

void StreamConductor::Close() {
  RTC_LOG(LS_INFO) << "StreamConductor closing";

  if (video_source_) {
    video_source_->Stop();
  }

  client_->SignOut();
  DeletePeerConnection();

  while (!pending_messages_.empty()) {
    delete pending_messages_.front();
    pending_messages_.pop_front();
  }
}

bool StreamConductor::CreatePeerConnection() {
  RTC_DCHECK(peer_connection_factory_);

  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

  // Add STUN server if configured (needed for namespace/NAT scenarios)
  if (!stun_server_.empty()) {
    webrtc::PeerConnectionInterface::IceServer ice_server;
    ice_server.uri = stun_server_;
    config.servers.push_back(ice_server);
    printf("[stream_client] Using STUN server: %s\n", stun_server_.c_str()); fflush(stdout);
    RTC_LOG(LS_INFO) << "Using STUN server: " << stun_server_;
  }

  webrtc::PeerConnectionDependencies pc_dependencies(this);
  auto error_or_peer_connection =
      peer_connection_factory_->CreatePeerConnectionOrError(
          config, std::move(pc_dependencies));

  if (error_or_peer_connection.ok()) {
    peer_connection_ = std::move(error_or_peer_connection.value());
  }

  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Failed to create PeerConnection";
    return false;
  }

  AddVideoTrack();

  return true;
}

void StreamConductor::DeletePeerConnection() {
  peer_connection_ = nullptr;
  peer_connection_factory_ = nullptr;
  video_source_ = nullptr;
  peer_id_ = -1;
  remote_description_set_ = false;
  pending_ice_candidates_.clear();
}

void StreamConductor::AddVideoTrack() {
  RTC_DCHECK(peer_connection_factory_);
  RTC_DCHECK(peer_connection_);

  video_source_ = webrtc::make_ref_counted<CustomVideoSource>(width_, height_, fps_);

  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track =
      peer_connection_factory_->CreateVideoTrack(video_source_, kVideoLabel);

  auto result = peer_connection_->AddTrack(video_track, {kStreamId});
  if (!result.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to add video track: "
                      << result.error().message();
    return;
  }

  video_source_->Start();

  printf("[stream_client] Video track added, frame generation started\n"); fflush(stdout);
  RTC_LOG(LS_INFO) << "Video track added and frame generation started ("
                    << width_ << "x" << height_ << " @ " << fps_ << "fps)";
}

void StreamConductor::SendMessage(const std::string& json_object) {
  std::string* msg = new std::string(json_object);
  pending_messages_.push_back(msg);
  ProcessPendingMessages();
}

void StreamConductor::ProcessPendingMessages() {
  while (!pending_messages_.empty() && !client_->IsSendingMessage()) {
    std::string* msg = pending_messages_.front();
    pending_messages_.pop_front();

    if (!client_->SendToPeer(peer_id_, *msg)) {
      RTC_LOG(LS_ERROR) << "SendToPeer failed";
    }
    delete msg;
  }
}

//
// PeerConnectionObserver
//

void StreamConductor::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  RTC_LOG(LS_INFO) << "ICE connection state changed: " << new_state;
  if (new_state == webrtc::PeerConnectionInterface::kIceConnectionFailed) {
    RTC_LOG(LS_WARNING) << "ICE connection failed";
  }
}

void StreamConductor::OnIceCandidate(const webrtc::IceCandidate* candidate) {
  RTC_LOG(LS_INFO) << "New ICE candidate";

  Json::Value jmessage;
  jmessage[kCandidateSdpMidName] = candidate->sdp_mid();
  jmessage[kCandidateSdpMlineIndexName] = candidate->sdp_mline_index();
  jmessage[kCandidateSdpName] = candidate->ToString();

  Json::StreamWriterBuilder factory;
  SendMessage(Json::writeString(factory, jmessage));
}

//
// PeerConnectionClientObserver
//

void StreamConductor::OnSignedIn() {
  printf("[stream_client] Signed in to server\n"); fflush(stdout);
  RTC_LOG(LS_INFO) << "Signed in to server as '" << stream_id_ << "'";
  RTC_LOG(LS_INFO) << "Waiting for a receiver to connect...";
}

void StreamConductor::OnDisconnected() {
  printf("[stream_client] Disconnected\n"); fflush(stdout);
  RTC_LOG(LS_INFO) << "Disconnected from server";
  DeletePeerConnection();
}

void StreamConductor::OnPeerConnected(int id, const std::string& name) {
  printf("[stream_client] Peer connected: %s (id=%d)\n", name.c_str(), id); fflush(stdout);
  RTC_LOG(LS_INFO) << "Peer connected: " << name << " (id=" << id << ")";
  if (peer_id_ != -1 && peer_id_ != id) {
    printf("[stream_client] Already connected to peer %d, sending BYE\n", peer_id_); fflush(stdout);
    client_->SendHangUp(peer_id_);
    OnPeerDisconnected(peer_id_);
  }
}

void StreamConductor::OnPeerDisconnected(int id) {
  printf("[stream_client] Peer disconnected: id=%d\n", id); fflush(stdout);
  if (id == peer_id_) {
    printf("[stream_client] Cleaning up connection...\n"); fflush(stdout);

    if (video_source_) {
      video_source_->Stop();
    }

    if (peer_connection_) {
      peer_connection_->Close();
    }

    usleep(500000);

    peer_connection_ = nullptr;
    video_source_ = nullptr;
    peer_connection_factory_ = nullptr;

    peer_id_ = -1;
    remote_description_set_ = false;
    pending_ice_candidates_.clear();

    printf("[stream_client] Ready for new receiver...\n"); fflush(stdout);
  }
}

void StreamConductor::OnMessageFromPeer(int peer_id,
                                         const std::string& message) {
  printf("[stream_client] Message from peer %d (len=%zu)\n", peer_id, message.length()); fflush(stdout);
  RTC_LOG(LS_INFO) << "Message from peer " << peer_id;

  if (!peer_connection_) {
    if (!peer_connection_factory_) {
      printf("[stream_client] Re-initializing PeerConnectionFactory...\n"); fflush(stdout);
      if (!Initialize()) {
        printf("[stream_client] ERROR: Failed to re-initialize\n"); fflush(stdout);
        return;
      }
    }
    peer_id_ = peer_id;
    if (!CreatePeerConnection()) {
      printf("[stream_client] ERROR: Failed to create PeerConnection\n"); fflush(stdout);
      return;
    }
  } else if (peer_id != peer_id_) {
    RTC_LOG(LS_WARNING) << "Message from unknown peer " << peer_id
                         << " (expected " << peer_id_ << ")";
    return;
  }

  Json::CharReaderBuilder factory;
  std::unique_ptr<Json::CharReader> reader =
      absl::WrapUnique(factory.newCharReader());
  Json::Value jmessage;
  if (!reader->parse(message.data(), message.data() + message.length(),
                     &jmessage, nullptr)) {
    RTC_LOG(LS_WARNING) << "Failed to parse message: " << message;
    return;
  }

  std::string type_str;
  webrtc::GetStringFromJsonObject(jmessage, kSessionDescriptionTypeName,
                                  &type_str);

  if (!type_str.empty()) {
    std::optional<webrtc::SdpType> type_maybe =
        webrtc::SdpTypeFromString(type_str);
    if (!type_maybe) {
      RTC_LOG(LS_ERROR) << "Unknown SDP type: " << type_str;
      return;
    }

    webrtc::SdpType type = *type_maybe;
    std::string sdp;
    if (!webrtc::GetStringFromJsonObject(jmessage, kSessionDescriptionSdpName,
                                         &sdp)) {
      RTC_LOG(LS_WARNING) << "Can't parse SDP from message";
      return;
    }

    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::SessionDescriptionInterface> session_description =
        webrtc::CreateSessionDescription(type, sdp, &error);
    if (!session_description) {
      RTC_LOG(LS_WARNING) << "Can't parse SDP: " << error.description;
      return;
    }

    printf("[stream_client] Received %s from peer %d\n", type_str.c_str(), peer_id); fflush(stdout);
    RTC_LOG(LS_INFO) << "Received " << type_str << " from peer " << peer_id;

    remote_description_set_ = false;
    peer_connection_->SetRemoteDescription(
        DummySetSessionDescriptionObserver::Create().get(),
        session_description.release());

    if (type == webrtc::SdpType::kOffer) {
      printf("[stream_client] Creating answer...\n"); fflush(stdout);
      RTC_LOG(LS_INFO) << "Creating answer...";
      peer_connection_->CreateAnswer(
          this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    }
  } else {
    if (!remote_description_set_) {
      RTC_LOG(LS_INFO) << "Queuing ICE candidate (remote description not set yet)";
      pending_ice_candidates_.push_back(message);
      return;
    }

    std::string sdp_mid;
    int sdp_mlineindex = 0;
    std::string sdp;
    if (!webrtc::GetStringFromJsonObject(jmessage, kCandidateSdpMidName,
                                         &sdp_mid) ||
        !webrtc::GetIntFromJsonObject(jmessage, kCandidateSdpMlineIndexName,
                                      &sdp_mlineindex) ||
        !webrtc::GetStringFromJsonObject(jmessage, kCandidateSdpName, &sdp)) {
      RTC_LOG(LS_WARNING) << "Can't parse ICE candidate message";
      return;
    }

    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidate> candidate(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error));
    if (!candidate) {
      RTC_LOG(LS_WARNING) << "Can't parse ICE candidate: " << error.description;
      return;
    }

    if (!peer_connection_->AddIceCandidate(candidate.get())) {
      RTC_LOG(LS_WARNING) << "Failed to add ICE candidate";
    } else {
      RTC_LOG(LS_INFO) << "Added ICE candidate from peer " << peer_id;
    }
  }
}

void StreamConductor::OnMessageSent(int err) {
  ProcessPendingMessages();
}

void StreamConductor::OnServerConnectionFailure() {
  printf("[stream_client] ERROR: Failed to connect to server\n"); fflush(stdout);
  RTC_LOG(LS_ERROR) << "Failed to connect to server " << server_;
}

//
// CreateSessionDescriptionObserver
//

void StreamConductor::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
  printf("[stream_client] SDP created successfully\n"); fflush(stdout);
  RTC_LOG(LS_INFO) << "SDP created successfully ("
                    << webrtc::SdpTypeToString(desc->GetType()) << ")";

  peer_connection_->SetLocalDescription(
      DummySetSessionDescriptionObserver::Create().get(), desc);

  std::string sdp;
  desc->ToString(&sdp);

  Json::Value jmessage;
  jmessage[kSessionDescriptionTypeName] =
      webrtc::SdpTypeToString(desc->GetType());
  jmessage[kSessionDescriptionSdpName] = sdp;

  Json::StreamWriterBuilder factory;
  SendMessage(Json::writeString(factory, jmessage));

  remote_description_set_ = true;
  if (!pending_ice_candidates_.empty()) {
    RTC_LOG(LS_INFO) << "Processing " << pending_ice_candidates_.size()
                      << " queued ICE candidates";
    for (const auto& ice_msg : pending_ice_candidates_) {
      OnMessageFromPeer(peer_id_, ice_msg);
    }
    pending_ice_candidates_.clear();
  }
}

void StreamConductor::OnFailure(webrtc::RTCError error) {
  RTC_LOG(LS_ERROR) << "SDP creation failed: " << error.message();
}

}  // namespace stream_client
