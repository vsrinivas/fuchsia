// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_GEOMETRY_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_GEOMETRY_H_

#include <stdint.h>

#include <string>

#include "chromium_utils.h"

namespace gfx {
class Size {
 public:
  constexpr Size() : width_(0), height_(0) {}
  constexpr Size(int width, int height)
      : width_(std::max(0, width)), height_(std::max(0, height)) {}

  constexpr int width() const { return width_; }
  constexpr int height() const { return height_; }

  void set_width(int width) { width_ = std::max(0, width); }
  void set_height(int height) { height_ = std::max(0, height); }

  bool IsEmpty() const { return width_ == 0 || height_ == 0; }
  std::string ToString() { return std::string(); }

 private:
  int width_;
  int height_;
};

inline bool operator==(const Size& lhs, const Size& rhs) {
  return lhs.width() == rhs.width() && lhs.height() == rhs.height();
}

inline bool operator!=(const Size& lhs, const Size& rhs) {
  return !(lhs == rhs);
}

class Point {
 public:
  constexpr Point() : x_(0), y_(0) {}
  constexpr Point(int x, int y) : x_(x), y_(y) {}

  constexpr int x() const { return x_; }
  constexpr int y() const { return y_; }
  void set_x(int x) { x_ = x; }
  void set_y(int y) { y_ = y; }
  std::string ToString() { return std::string(); }

 private:
  int x_;
  int y_;
};

inline bool operator==(const Point& lhs, const Point& rhs) {
  return lhs.x() == rhs.x() && lhs.y() == rhs.y();
}

inline bool operator!=(const Point& lhs, const Point& rhs) {
  return !(lhs == rhs);
}

class Rect {
 public:
  constexpr Rect() = default;

  constexpr Rect(int width, int height) : size_(width, height) {}

  constexpr Rect(int x, int y, int width, int height)
      : origin_(x, y), size_(width, height) {}

  constexpr explicit Rect(const Size& size) : size_(size) {}

  constexpr int x() const { return origin_.x(); }
  void set_x(int x) { origin_.set_x(x); }

  constexpr int y() const { return origin_.y(); }
  void set_y(int y) { origin_.set_y(y); }

  constexpr int width() const { return size_.width(); }
  void set_width(int width) { size_.set_width(width); }

  constexpr int height() const { return size_.height(); }
  void set_height(int height) { size_.set_height(height); }

  constexpr const Point& origin() const { return origin_; }
  void set_origin(const Point& origin) { origin_ = origin; }

  constexpr const Size& size() const { return size_; }
  void set_size(const Size& size) {
    set_width(size.width());
    set_height(size.height());
  }

  std::string ToString() { return std::string(); }

 private:
  gfx::Point origin_;
  gfx::Size size_;
};

inline bool operator==(const Rect& lhs, const Rect& rhs) {
  return lhs.origin() == rhs.origin() && lhs.size() == rhs.size();
}

inline bool operator!=(const Rect& lhs, const Rect& rhs) {
  return !(lhs == rhs);
}

}  // namespace gfx

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_GEOMETRY_H_
