/*
 *  Copyright (c) 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  YUV420 frame generator with timestamp overlay, bouncing rectangle,
 *  and draggable circle (remote control simulation).
 */

#ifndef EXAMPLES_PEERCONNECTION_STREAM_CLIENT_FRAME_GENERATOR_H_
#define EXAMPLES_PEERCONNECTION_STREAM_CLIENT_FRAME_GENERATOR_H_

#include <cstdint>
#include <string>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "rtc_base/synchronization/mutex.h"

namespace stream_client {

class SimpleFont {
 public:
  static const uint8_t* GetGlyph(char c);
  static constexpr int kWidth = 5;
  static constexpr int kHeight = 7;
};

class FrameGenerator {
 public:
  FrameGenerator(int width, int height, int fps);
  ~FrameGenerator();

  webrtc::scoped_refptr<webrtc::I420Buffer> GenerateFrame(int64_t elapsed_ms);

  // Set the circle center position (called from DataChannel mouse events).
  void SetCirclePosition(int x, int y);

  int width() const { return width_; }
  int height() const { return height_; }
  int fps() const { return fps_; }

 private:
  void DrawChar(uint8_t* y_plane, int stride, int x, int y, char c,
                uint8_t color);
  void DrawString(uint8_t* y_plane, int stride, int x, int y,
                  const std::string& text, uint8_t color);
  std::string FormatTimestamp(int64_t elapsed_ms);
  void DrawRect(uint8_t* y_plane, uint8_t* u_plane, uint8_t* v_plane,
                int y_stride, int uv_stride, int x, int y, int w, int h,
                uint8_t y_val, uint8_t u_val, uint8_t v_val);
  void DrawCircle(uint8_t* y_plane, uint8_t* u_plane, uint8_t* v_plane,
                  int y_stride, int uv_stride, int cx, int cy, int radius,
                  uint8_t y_val, uint8_t u_val, uint8_t v_val);
  void ClearFrame(uint8_t* y_plane, uint8_t* u_plane, uint8_t* v_plane,
                  int y_stride, int uv_stride);

  int width_;
  int height_;
  int fps_;

  // Bouncing rectangle state
  int rect_x_;
  int rect_dir_;
  int rect_size_;
  int frame_count_;

  // Draggable circle state (protected by mutex for cross-thread access)
  int circle_x_;
  int circle_y_;
  int circle_radius_;
  webrtc::Mutex circle_mutex_;

  webrtc::scoped_refptr<webrtc::I420Buffer> buffer_;
};

}  // namespace stream_client

#endif  // EXAMPLES_PEERCONNECTION_STREAM_CLIENT_FRAME_GENERATOR_H_
