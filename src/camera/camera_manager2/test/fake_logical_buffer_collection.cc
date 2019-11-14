// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_logical_buffer_collection.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>

#include <src/lib/syslog/cpp/logger.h>

namespace camera {
void FakeLogicalBufferCollection::WaitForBuffersAllocated(
    fit::function<void(int32_t, fuchsia::sysmem::BufferCollectionInfo_2)> callback) {
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;  // The buffers returned are empty.
  if (give_error_) {
    callback(ZX_ERR_NO_MEMORY, std::move(buffers));
  } else {
    callback(ZX_OK, std::move(buffers));
  }
}

fidl::InterfaceHandle<fuchsia::sysmem::BufferCollection>
FakeLogicalBufferCollection::GetBufferCollection() {
  ZX_ASSERT(!binding_.is_bound());
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollection> client;
  FX_CHECK(binding_.Bind(client.NewRequest(), dispatcher_) == ZX_OK);
  closed_ = false;
  return client;
}
}  // namespace camera
