// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_SCOPED_CANVAS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_SCOPED_CANVAS_H_

#include <ddktl/protocol/amlogiccanvas.h>

// Move-only amlogic canvas ID wrappper.
class ScopedCanvasId {
 public:
  ScopedCanvasId() = default;
  ScopedCanvasId(const ddk::AmlogicCanvasProtocolClient* canvas, uint8_t id)
      : canvas_(canvas), id_(id) {}
  ScopedCanvasId(ScopedCanvasId&& other);
  ScopedCanvasId(const ScopedCanvasId&) = delete;

  ScopedCanvasId& operator=(ScopedCanvasId&& other);
  ScopedCanvasId& operator=(ScopedCanvasId&) = delete;

  ~ScopedCanvasId() { Reset(); }

  void Reset();
  uint8_t id() const { return id_; }
  bool valid() const { return static_cast<bool>(canvas_); }

 private:
  const ddk::AmlogicCanvasProtocolClient* canvas_ = nullptr;
  uint8_t id_ = 0;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_SCOPED_CANVAS_H_
