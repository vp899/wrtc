/*
 *  Copyright (c) 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  Custom video track source that generates YUV420 frames with animation.
 *  Supports remote circle position control via SetCirclePosition().
 */

#ifndef EXAMPLES_PEERCONNECTION_STREAM_CLIENT_CUSTOM_VIDEO_SOURCE_H_
#define EXAMPLES_PEERCONNECTION_STREAM_CLIENT_CUSTOM_VIDEO_SOURCE_H_

#include <atomic>
#include <memory>
#include <thread>

#include "api/scoped_refptr.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "examples/peerconnection/stream_client/frame_generator.h"
#include "pc/video_track_source.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/timestamp_aligner.h"

namespace stream_client {

class CustomVideoSource : public webrtc::VideoTrackSource {
 public:
  CustomVideoSource(int width, int height, int fps);
  ~CustomVideoSource() override;

  void Start();
  void Stop();

  // Set the draggable circle position (called from DataChannel).
  void SetCirclePosition(int x, int y);

  bool is_screencast() const override { return false; }
  std::optional<bool> needs_denoising() const override { return false; }

 protected:
  webrtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
    return this;
  }
  void AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
                       const webrtc::VideoSinkWants& wants) override;
  void RemoveSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override;

 private:
  void CaptureThreadFunc();

  std::unique_ptr<FrameGenerator> frame_generator_;
  int width_;
  int height_;
  int fps_;

  std::atomic<bool> running_;
  std::unique_ptr<std::thread> capture_thread_;

  webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink_;
  webrtc::TimestampAligner timestamp_aligner_;

  webrtc::Mutex mutex_;
};

}  // namespace stream_client

#endif  // EXAMPLES_PEERCONNECTION_STREAM_CLIENT_CUSTOM_VIDEO_SOURCE_H_
