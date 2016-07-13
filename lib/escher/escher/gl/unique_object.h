// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>

#include "ftl/macros.h"
#include "escher/gl/bindings.h"

namespace escher {

typedef void (*ObjectDeleter)(const GLuint id);

template <ObjectDeleter Delete>
class UniqueObject {
 public:
  UniqueObject() = default;
  ~UniqueObject() { Reset(); }

  UniqueObject(UniqueObject<Delete>&& other) : id_(other.id_) { other.id_ = 0; }

  UniqueObject<Delete>& operator=(UniqueObject<Delete>&& other) {
    std::swap(id_, other.id_);
    return *this;
  }

  explicit operator bool() const { return id_ != 0; }

  GLuint id() const { return id_; }

  void Reset(GLuint id = 0) {
    GLuint previous_id = id_;
    id_ = id;
    if (previous_id)
      Delete(previous_id);
  }

 private:
  GLuint id_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(UniqueObject);
};

}  // namespace escher
