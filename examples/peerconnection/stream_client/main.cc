/*
 *  Copyright (c) 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  stream_client - Headless video sender for WebRTC streaming.
 *
 *  Generates YUV420p frames (320x240 @ 30fps) with:
 *    - Timestamp overlay (HH:MM:SS:mmm)
 *    - 100x100 bouncing green rectangle
 *
 *  Encodes via WebRTC's built-in OpenH264 encoder and streams to
 *  a web browser receiver via peerconnection_server signaling.
 *
 *  Usage:
 *    ./stream_client --server=<server_ip> --port=8888 --id=stream_001
 *    ./stream_client --server=192.168.1.100 --port=8888 --id=stream_001 \
 *                    --width=320 --height=240 --fps=30
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials.h"
#include "api/make_ref_counted.h"
#include "examples/peerconnection/stream_client/peer_connection_client.h"
#include "examples/peerconnection/stream_client/stream_conductor.h"
#include "rtc_base/checks.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"

// Command line flags
ABSL_FLAG(std::string, server, "localhost",
          "The peerconnection server address");

ABSL_FLAG(int, port, 8888,
          "The peerconnection server port (default: 8888)");

ABSL_FLAG(std::string, id, "stream_001",
          "The fixed stream ID (client name for signaling)");

ABSL_FLAG(int, width, 320,
          "Video frame width (default: 320)");

ABSL_FLAG(int, height, 240,
          "Video frame height (default: 240)");

ABSL_FLAG(int, fps, 30,
          "Video frame rate (default: 30)");

ABSL_FLAG(std::string, stun, "",
          "STUN server URI for ICE candidate gathering (e.g. stun:192.168.96.129:3478). "
          "Required when running inside a network namespace.");

ABSL_FLAG(std::string, force_fieldtrials, "",
          "Field trials for experimental features");

namespace {
volatile int g_running = true;

void SignalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    g_running = false;
  }
}
}  // namespace

int main(int argc, char* argv[]) {
  absl::SetProgramUsageMessage(
      "Usage: ./stream_client --server=<ip> --port=8888 --id=<stream_id>\n"
      "\n"
      "A headless WebRTC video sender that generates animated YUV420 frames\n"
      "and streams them via peerconnection_server to a web browser receiver.\n"
      "\n"
      "Example:\n"
      "  ./stream_client --server=192.168.1.100 --port=8888 --id=stream_001\n"
      "  ./stream_client --server=192.168.1.100 --port=8888 --id=stream_001 \\\n"
      "                  --stun=stun:192.168.1.100:3478");

  absl::ParseCommandLine(argc, argv);

  // Install signal handler for graceful shutdown
  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);

  // Validate parameters
  std::string server = absl::GetFlag(FLAGS_server);
  int port = absl::GetFlag(FLAGS_port);
  std::string stream_id = absl::GetFlag(FLAGS_id);
  int width = absl::GetFlag(FLAGS_width);
  int height = absl::GetFlag(FLAGS_height);
  int fps = absl::GetFlag(FLAGS_fps);
  std::string stun = absl::GetFlag(FLAGS_stun);

  if (server.empty()) {
    fprintf(stderr, "Error: server address is required\n");
    return 1;
  }
  if (stream_id.empty()) {
    fprintf(stderr, "Error: stream ID is required\n");
    return 1;
  }
  if (port < 1 || port > 65535) {
    fprintf(stderr, "Error: port must be between 1 and 65535\n");
    return 1;
  }
  if (width < 1 || height < 1) {
    fprintf(stderr, "Error: invalid frame dimensions\n");
    return 1;
  }
  if (fps < 1 || fps > 120) {
    fprintf(stderr, "Error: fps must be between 1 and 120\n");
    return 1;
  }

  printf("=== stream_client ===\n");
  printf("Server:     %s:%d\n", server.c_str(), port);
  printf("Stream ID:  %s\n", stream_id.c_str());
  printf("Resolution: %dx%d @ %dfps\n", width, height, fps);
  printf("Codec:      H264 (OpenH264)\n");
  if (!stun.empty()) {
    printf("STUN:       %s\n", stun.c_str());
  }
  printf("\nPress Ctrl+C to stop.\n\n");

  // Initialize WebRTC
  webrtc::PhysicalSocketServer ss;
  std::unique_ptr<webrtc::Thread> main_thread =
      std::make_unique<webrtc::Thread>(&ss);
  webrtc::ThreadManager::Instance()->SetCurrentThread(main_thread.get());

  webrtc::Environment env =
      webrtc::CreateEnvironment(std::make_unique<webrtc::FieldTrials>(
          absl::GetFlag(FLAGS_force_fieldtrials)));

  webrtc::InitializeSSL();

  // Create components
  PeerConnectionClient client;
  auto conductor = webrtc::make_ref_counted<stream_client::StreamConductor>(
      env, &client, stream_id, width, height, fps);

  // Set STUN server if provided
  if (!stun.empty()) {
    conductor.get()->SetStunServer(stun);
  }

  // Initialize conductor
  if (!conductor.get()->Initialize()) {
    fprintf(stderr, "Error: Failed to initialize StreamConductor\n");
    return 1;
  }

  // Connect to signaling server
  printf("Connecting to signaling server %s:%d...\n", server.c_str(), port);
  conductor.get()->ConnectToServer(server, port);

  // Main loop - process messages until signal
  while (g_running) {
    main_thread->ProcessMessages(100);  // 100ms timeout
  }

  // Cleanup
  printf("\nShutting down...\n");
  conductor.get()->Close();

  webrtc::CleanupSSL();
  webrtc::ThreadManager::Instance()->SetCurrentThread(nullptr);

  printf("Done.\n");
  return 0;
}
