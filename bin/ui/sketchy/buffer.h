// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/scenic/client/resources.h"
#include "apps/mozart/lib/scenic/client/session.h"
#include "escher/vk/buffer_factory.h"

namespace sketchy_service {

// Buffer encapsulates an Escher buffer and a Scenic buffer, which share the
// same memory.  The Escher buffer is exported as a VMO, which is used to
// create the Scenic buffer.
class Buffer {
 public:
  Buffer(scenic_lib::Session* session,
         escher::BufferFactory* factory,
         vk::DeviceSize size);

  Buffer(scenic_lib::Session* session, escher::BufferPtr buffer);

  const escher::BufferPtr& escher_buffer() const { return escher_buffer_; }
  const scenic_lib::Buffer& scenic_buffer() const { return scenic_buffer_; }

 private:
  escher::BufferPtr escher_buffer_;
  scenic_lib::Buffer scenic_buffer_;
};

}  // namespace sketchy_service
