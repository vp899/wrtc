/*
 *  Copyright (c) 2024 The WebRTC Project Authors. All rights reserved.
 *
 *  YUV420 frame generator implementation.
 */

#include "examples/peerconnection/stream_client/frame_generator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "api/video/i420_buffer.h"
#include "rtc_base/logging.h"

namespace stream_client {

namespace {

// 5x7 bitmap font data for digits 0-9, colon (:), dot (.), dash (-)
// Each character is 7 rows, each row is 5 bits (bit 0 = leftmost pixel)
const uint8_t kFontData[][7] = {
    // '0'
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
    // '1'
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    // '2'
    {0b01110, 0b10001, 0b00001, 0b00110, 0b01000, 0b10000, 0b11111},
    // '3'
    {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110},
    // '4'
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
    // '5'
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
    // '6'
    {0b01110, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b01110},
    // '7'
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
    // '8'
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
    // '9'
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b01110},
    // ':' (index 10)
    {0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000},
    // '.' (index 11)
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100},
    // '-' (index 12)
    {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000},
};

const int kNumFontChars = sizeof(kFontData) / sizeof(kFontData[0]);

int CharToFontIndex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c == ':') return 10;
  if (c == '.') return 11;
  if (c == '-') return 12;
  return -1;
}

}  // namespace

const uint8_t* SimpleFont::GetGlyph(char c) {
  int idx = CharToFontIndex(c);
  if (idx < 0 || idx >= kNumFontChars) return nullptr;
  return kFontData[idx];
}

FrameGenerator::FrameGenerator(int width, int height, int fps)
    : width_(width),
      height_(height),
      fps_(fps),
      rect_x_(0),
      rect_dir_(1),
      rect_size_(100),
      frame_count_(0) {
  buffer_ = webrtc::I420Buffer::Create(width_, height_);
}

FrameGenerator::~FrameGenerator() {}

std::string FrameGenerator::FormatTimestamp(int64_t elapsed_ms) {
  int total_seconds = static_cast<int>(elapsed_ms / 1000);
  int hours = total_seconds / 3600;
  int minutes = (total_seconds % 3600) / 60;
  int seconds = total_seconds % 60;
  int millis = static_cast<int>(elapsed_ms % 1000);

  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%03d", hours, minutes, seconds,
           millis);
  return std::string(buf);
}

void FrameGenerator::DrawChar(uint8_t* y_plane, int stride, int x, int y,
                               char c, uint8_t color) {
  const uint8_t* glyph = SimpleFont::GetGlyph(c);
  if (!glyph) return;

  for (int row = 0; row < SimpleFont::kHeight; ++row) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < SimpleFont::kWidth; ++col) {
      int px = x + col;
      int py = y + row;
      if (px >= 0 && px < width_ && py >= 0 && py < height_) {
        if (bits & (1 << (SimpleFont::kWidth - 1 - col))) {
          y_plane[py * stride + px] = color;
        }
      }
    }
  }
}

void FrameGenerator::DrawString(uint8_t* y_plane, int stride, int x, int y,
                                 const std::string& text, uint8_t color) {
  int cx = x;
  for (char c : text) {
    DrawChar(y_plane, stride, cx, y, c, color);
    cx += SimpleFont::kWidth + 1;
  }
}

void FrameGenerator::DrawRect(uint8_t* y_plane, uint8_t* u_plane,
                               uint8_t* v_plane, int y_stride, int uv_stride,
                               int x, int y, int w, int h, uint8_t y_val,
                               uint8_t u_val, uint8_t v_val) {
  int x0 = std::max(0, x);
  int y0 = std::max(0, y);
  int x1 = std::min(width_, x + w);
  int y1 = std::min(height_, y + h);

  for (int row = y0; row < y1; ++row) {
    for (int col = x0; col < x1; ++col) {
      y_plane[row * y_stride + col] = y_val;
    }
  }

  int ux0 = x0 / 2;
  int uy0 = y0 / 2;
  int ux1 = (x1 + 1) / 2;
  int uy1 = (y1 + 1) / 2;

  for (int row = uy0; row < uy1; ++row) {
    for (int col = ux0; col < ux1; ++col) {
      u_plane[row * uv_stride + col] = u_val;
      v_plane[row * uv_stride + col] = v_val;
    }
  }
}

void FrameGenerator::ClearFrame(uint8_t* y_plane, uint8_t* u_plane,
                                 uint8_t* v_plane, int y_stride,
                                 int uv_stride) {
  memset(y_plane, 16, height_ * y_stride);
  memset(u_plane, 128, (height_ / 2) * uv_stride);
  memset(v_plane, 128, (height_ / 2) * uv_stride);
}

webrtc::scoped_refptr<webrtc::I420Buffer> FrameGenerator::GenerateFrame(
    int64_t elapsed_ms) {
  frame_count_++;
  rect_x_ += rect_dir_;

  if (rect_x_ + rect_size_ >= width_) {
    rect_x_ = width_ - rect_size_;
    rect_dir_ = -1;
  } else if (rect_x_ <= 0) {
    rect_x_ = 0;
    rect_dir_ = 1;
  }

  uint8_t* y_plane = buffer_->MutableDataY();
  uint8_t* u_plane = buffer_->MutableDataU();
  uint8_t* v_plane = buffer_->MutableDataV();
  int y_stride = buffer_->StrideY();
  int uv_stride = buffer_->StrideU();

  ClearFrame(y_plane, u_plane, v_plane, y_stride, uv_stride);

  std::string timestamp = FormatTimestamp(elapsed_ms);
  DrawString(y_plane, y_stride, 10, 10, timestamp, 235);

  DrawRect(y_plane, u_plane, v_plane, y_stride, uv_stride, rect_x_, 70,
           rect_size_, rect_size_, 150, 50, 100);

  return buffer_;
}

}  // namespace stream_client
