// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_IMAGE_COMPRESSION_IMAGE_COMPRESSION_H_
#define SRC_UI_SCENIC_LIB_IMAGE_COMPRESSION_IMAGE_COMPRESSION_H_

#include <fidl/fuchsia.ui.compression.internal/cpp/fidl.h>
#include <fidl/fuchsia.ui.compression.internal/cpp/hlcpp_conversion.h>
#include <fuchsia/ui/compression/internal/cpp/fidl.h>
#include <lib/async/dispatcher.h>

namespace image_compression {

// This is the component's main class. It holds all of the component's state.
class App {
 public:
  explicit App(async_dispatcher_t* dispatcher);

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
};

// This class implements the ImageCompressor protocol. It is stateless.
class ImageCompression : public fidl::Server<fuchsia_ui_compression_internal::ImageCompressor> {
 public:
  ImageCompression() = default;

  // |fidl::Server<fuchsia_ui_compression_internal::ImageCompressor>|
  void EncodePng(EncodePngRequest& request, EncodePngCompleter::Sync& completer) override;
};

}  // namespace image_compression

#endif  // SRC_UI_SCENIC_LIB_IMAGE_COMPRESSION_IMAGE_COMPRESSION_H_
