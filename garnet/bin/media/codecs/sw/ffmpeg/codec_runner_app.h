// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_RUNNER_APP_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_RUNNER_APP_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>

#include "local_single_codec_factory.h"

class CodecRunnerApp {
 public:
  CodecRunnerApp();

  void Run();

 private:
  async::Loop loop_;
  std::unique_ptr<component::StartupContext> startup_context_;
  std::unique_ptr<LocalSingleCodecFactory> codec_factory_;
  std::unique_ptr<CodecImpl> codec_instance_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_RUNNER_APP_H_