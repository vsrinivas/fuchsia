// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_CODEC_RUNNER_APP_H_
#define GARNET_BIN_MEDIA_CODECS_SW_CODEC_RUNNER_APP_H_

#include <memory>

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/interface_request.h>
#include <src/lib/fxl/logging.h>

#include "local_single_codec_factory.h"

// If a software can only provide an encoder or decoder, the other should be
// assigned NoAdapter in the template arguments, e.g.:
//   CodecRunnerApp<CodecAdapterFfmpeg, NoAdapter>
template <typename Decoder, typename Encoder>
class CodecRunnerApp {
 public:
  CodecRunnerApp()
      : loop_(&kAsyncLoopConfigAttachToThread),
        startup_context_(component::StartupContext::CreateFromStartupInfo()) {}

  void Run() {
    // Lives until after Loop.Run() which is fine since it is not referenced
    // after Loop.Run().
    auto codec_admission_control =
        std::make_unique<CodecAdmissionControl>(loop_.dispatcher());

    startup_context_->outgoing().deprecated_services()->AddService(
        fidl::InterfaceRequestHandler<fuchsia::mediacodec::CodecFactory>(
            [this, codec_admission_control = codec_admission_control.get()](
                fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory>
                    request) {
              // This runner only expects a single Local Codec Factory to ever
              // be requested.
              FXL_DCHECK(!codec_factory_);
              startup_context_->outgoing()
                  .deprecated_services()
                  ->RemoveService<fuchsia::mediacodec::CodecFactory>();

              fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem;
              startup_context_->ConnectToEnvironmentService(sysmem.NewRequest());

              codec_factory_ =
                  std::make_unique<LocalSingleCodecFactory<Decoder, Encoder>>(
                      loop_.dispatcher(), std::move(sysmem), std::move(request),
                      [this](
                          std::unique_ptr<CodecImpl> created_codec_instance) {
                        // Own codec implementation and bind it.
                        codec_instance_ = std::move(created_codec_instance);
                        codec_instance_->BindAsync([this] {
                          // Drop codec implementation and close channel on
                          // error.
                          codec_instance_.reset();
                        });
                        // Drop factory and close factory channel.
                        codec_factory_.reset();
                      },
                      codec_admission_control,
                      [this](zx_status_t error) {
                        // Drop factory and close factory channel on error.
                        codec_factory_.reset();
                      });
            }));

    loop_.Run();
  }

 private:
  async::Loop loop_;
  std::unique_ptr<component::StartupContext> startup_context_;
  std::unique_ptr<LocalSingleCodecFactory<Decoder, Encoder>> codec_factory_;
  std::unique_ptr<CodecImpl> codec_instance_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_CODEC_RUNNER_APP_H_
