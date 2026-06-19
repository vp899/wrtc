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

const uint8_t kFontData[][7] = {
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    {0b01110, 0b10001, 0b00001, 0b00110, 0b01000, 0b10000, 0b11111},
    {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110},
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
    {0b01110, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b01110},
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b01110},
    {0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000},
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100},
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
      frame_count_(0),
      circle_x_(width / 2),
      circle_y_(height / 2),
      circle_radius_(50) {
  buffer_ = webrtc::I420Buffer::Create(width_, height_);
}

FrameGenerator::~FrameGenerator() {}

void FrameGenerator::SetCirclePosition(int x, int y) {
  webrtc::MutexLock lock(&circle_mutex_);
  // Clamp to keep circle within frame bounds
  circle_x_ = std::max(circle_radius_, std::min(width_ - circle_radius_, x));
  circle_y_ = std::max(circle_radius_, std::min(height_ - circle_radius_, y));
}

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

void FrameGenerator::DrawCircle(uint8_t* y_plane, uint8_t* u_plane,
                                 uint8_t* v_plane, int y_stride, int uv_stride,
                                 int cx, int cy, int radius, uint8_t y_val,
                                 uint8_t u_val, uint8_t v_val) {
  int x0 = std::max(0, cx - radius);
  int y0 = std::max(0, cy - radius);
  int x1 = std::min(width_, cx + radius);
  int y1 = std::min(height_, cy + radius);

  int r2 = radius * radius;

  for (int row = y0; row < y1; ++row) {
    for (int col = x0; col < x1; ++col) {
      int dx = col - cx;
      int dy = row - cy;
      if (dx * dx + dy * dy <= r2) {
        y_plane[row * y_stride + col] = y_val;
      }
    }
  }

  // UV planes (half resolution)
  int ux0 = x0 / 2;
  int uy0 = y0 / 2;
  int ux1 = (x1 + 1) / 2;
  int uy1 = (y1 + 1) / 2;
  int cx_uv = cx / 2;
  int cy_uv = cy / 2;
  int r_uv = radius / 2;
  int r2_uv = r_uv * r_uv;

  for (int row = uy0; row < uy1; ++row) {
    for (int col = ux0; col < ux1; ++col) {
      int dx = col - cx_uv;
      int dy = row - cy_uv;
      if (dx * dx + dy * dy <= r2_uv) {
        u_plane[row * uv_stride + col] = u_val;
        v_plane[row * uv_stride + col] = v_val;
      }
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

  // Draw timestamp
  std::string timestamp = FormatTimestamp(elapsed_ms);
  DrawString(y_plane, y_stride, 10, 10, timestamp, 235);

  // Draw draggable red circle (above the square)
  // Red in YUV: Y=76, U=84, V=255
  {
    webrtc::MutexLock lock(&circle_mutex_);
    DrawCircle(y_plane, u_plane, v_plane, y_stride, uv_stride,
               circle_x_, circle_y_, circle_radius_,
               76, 84, 255);
  }

  // Draw bouncing green rectangle (below the circle)
  DrawRect(y_plane, u_plane, v_plane, y_stride, uv_stride, rect_x_, 140,
           rect_size_, rect_size_, 150, 50, 100);

  return buffer_;
}

}  // namespace stream_client
