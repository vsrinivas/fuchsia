// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scoped_canvas.h"

ScopedCanvasId::ScopedCanvasId(ScopedCanvasId&& other) {
  canvas_ = other.canvas_;
  id_ = other.id_;
  other.canvas_ = nullptr;
}

ScopedCanvasId& ScopedCanvasId::operator=(ScopedCanvasId&& other) {
  Reset();
  canvas_ = other.canvas_;
  id_ = other.id_;
  other.canvas_ = nullptr;
  return *this;
}

void ScopedCanvasId::Reset() {
  if (canvas_) {
    canvas_->Free(id_);
  }
  canvas_ = nullptr;
  id_ = 0;
}
