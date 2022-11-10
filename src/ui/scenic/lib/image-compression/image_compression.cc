// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/image-compression/image_compression.h"

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>

namespace image_compression {

App::App(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {
  async::PostTask(dispatcher_, []() { std::cout << "Hello, Fuchsia!" << std::endl; });
}

void ImageCompression::EncodePng(EncodePngRequest& request, EncodePngCompleter::Sync& completer) {
  FX_NOTIMPLEMENTED();
}

}  // namespace image_compression
