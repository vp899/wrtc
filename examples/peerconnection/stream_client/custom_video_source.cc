/*
 *  Copyright (c) 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Custom video track source implementation.
 */

#include "examples/peerconnection/stream_client/custom_video_source.h"

#include <chrono>
#include <thread>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"

namespace stream_client {

CustomVideoSource::CustomVideoSource(int width, int height, int fps)
    : VideoTrackSource(/*remote=*/false),
      width_(width),
      height_(height),
      fps_(fps),
      running_(false),
      sink_(nullptr) {
  frame_generator_ = std::make_unique<FrameGenerator>(width, height, fps);
}

CustomVideoSource::~CustomVideoSource() {
  Stop();
}

void CustomVideoSource::Start() {
  if (running_.load()) return;

  running_.store(true);
  capture_thread_ =
      std::make_unique<std::thread>(&CustomVideoSource::CaptureThreadFunc, this);
  RTC_LOG(LS_INFO) << "CustomVideoSource started: " << width_ << "x" << height_
                    << " @ " << fps_ << "fps";
}

void CustomVideoSource::Stop() {
  running_.store(false);
  if (capture_thread_ && capture_thread_->joinable()) {
    capture_thread_->join();
  }
  capture_thread_.reset();
  RTC_LOG(LS_INFO) << "CustomVideoSource stopped";
}

void CustomVideoSource::AddOrUpdateSink(
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
    const webrtc::VideoSinkWants& wants) {
  webrtc::MutexLock lock(&mutex_);
  sink_ = sink;
}

void CustomVideoSource::RemoveSink(
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
  webrtc::MutexLock lock(&mutex_);
  if (sink_ == sink) {
    sink_ = nullptr;
  }
}

void CustomVideoSource::CaptureThreadFunc() {
  RTC_LOG(LS_INFO) << "Capture thread started";

  // Use steady clock for accurate timing
  auto start_time = std::chrono::steady_clock::now();
  int64_t frame_number = 0;
  const int64_t frame_interval_us = 1000000 / fps_;  // microseconds per frame

  while (running_.load()) {
    auto now = std::chrono::steady_clock::now();
    int64_t elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - start_time)
            .count();
    int64_t elapsed_ms = elapsed_us / 1000;

    // Generate the frame
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
        frame_generator_->GenerateFrame(elapsed_ms);

    // Create VideoFrame
    webrtc::VideoFrame frame =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(elapsed_us)
            .build();

    // Deliver frame to sink
    {
      webrtc::MutexLock lock(&mutex_);
      if (sink_) {
        sink_->OnFrame(frame);
      }
    }

    frame_number++;

    // Calculate next frame time and sleep
    int64_t next_frame_us = frame_number * frame_interval_us;
    int64_t sleep_us = next_frame_us - elapsed_us;

    if (sleep_us > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    } else {
      // We're behind, log warning occasionally
      if (frame_number % (fps_ * 5) == 0) {
        RTC_LOG(LS_WARNING) << "Frame generation running behind by "
                            << (-sleep_us / 1000) << "ms";
      }
    }
  }

  RTC_LOG(LS_INFO) << "Capture thread exiting after " << frame_number
                    << " frames";
}

}  // namespace stream_client
