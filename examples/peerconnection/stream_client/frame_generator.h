/*
 *  Copyright (c) 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  YUV420 frame generator with timestamp overlay and bouncing rectangle.
 *  Generates 320x240 @ 30fps frames for streaming.
 */

#ifndef EXAMPLES_PEERCONNECTION_STREAM_CLIENT_FRAME_GENERATOR_H_
#define EXAMPLES_PEERCONNECTION_STREAM_CLIENT_FRAME_GENERATOR_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"

namespace stream_client {

// A simple 5x7 bitmap font for digits, colon, dash, and dot.
// Each character is 5 pixels wide, 7 pixels tall.
class SimpleFont {
 public:
  // Returns the bitmap for a character. Each row is 5 bits (LSB = left).
  // Returns nullptr for unsupported characters.
  static const uint8_t* GetGlyph(char c);

  // Returns character width and height.
  static constexpr int kWidth = 5;
  static constexpr int kHeight = 7;
};

// Generates YUV420 frames with:
// 1. Timestamp overlay in HH:MM:SS:mmm format (top-left)
// 2. A 100x100 bouncing rectangle moving horizontally, 1 pixel/frame
//
// Frame dimensions: 320x240, YUV420p format
// Background: dark gray (Y=16, U=128, V=128)
// Timestamp: white (Y=235)
// Rectangle: green (Y=150, U=50, V=100)
class FrameGenerator {
 public:
  FrameGenerator(int width, int height, int fps);
  ~FrameGenerator();

  // Generate the next frame. Returns an I420Buffer.
  // `elapsed_ms` is the elapsed time in milliseconds since start.
  webrtc::scoped_refptr<webrtc::I420Buffer> GenerateFrame(int64_t elapsed_ms);

  int width() const { return width_; }
  int height() const { return height_; }
  int fps() const { return fps_; }

 private:
  // Draw a single character at (x, y) in the Y plane.
  void DrawChar(uint8_t* y_plane, int stride, int x, int y, char c,
                uint8_t color);

  // Draw a string at (x, y) in the Y plane.
  void DrawString(uint8_t* y_plane, int stride, int x, int y,
                  const std::string& text, uint8_t color);

  // Format elapsed milliseconds as HH:MM:SS:mmm
  std::string FormatTimestamp(int64_t elapsed_ms);

  // Fill the rectangle area in YUV planes.
  void DrawRect(uint8_t* y_plane, uint8_t* u_plane, uint8_t* v_plane,
                int y_stride, int uv_stride, int x, int y, int w, int h,
                uint8_t y_val, uint8_t u_val, uint8_t v_val);

  // Clear the frame to background color.
  void ClearFrame(uint8_t* y_plane, uint8_t* u_plane, uint8_t* v_plane,
                  int y_stride, int uv_stride);

  int width_;
  int height_;
  int fps_;

  // Bouncing rectangle state
  int rect_x_;       // Current X position of rectangle
  int rect_dir_;     // Direction: 1 = right, -1 = left
  int rect_size_;    // Rectangle size (100x100)
  int frame_count_;  // Frame counter for movement

  // Cached buffer for reuse
  webrtc::scoped_refptr<webrtc::I420Buffer> buffer_;
};

}  // namespace stream_client

#endif  // EXAMPLES_PEERCONNECTION_STREAM_CLIENT_FRAME_GENERATOR_H_
