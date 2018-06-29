// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_CODEC_RUNNER_COMPONENT_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_CODEC_RUNNER_COMPONENT_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"

namespace codec_runner {

class CodecRunnerComponent {
 public:
  CodecRunnerComponent(
      async_t* fidl_async, thrd_t fidl_thread,
      std::unique_ptr<fuchsia::sys::StartupContext> startup_context);

 private:
  async_t* fidl_async_ = nullptr;
  thrd_t fidl_thread_ = 0;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
};

}  // namespace codec_runner

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_CODEC_RUNNER_SW_OMX_CODEC_RUNNER_COMPONENT_H_
