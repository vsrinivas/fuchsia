// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_CODEC_RUNNER_APP_H_
#define GARNET_BIN_MEDIA_CODECS_SW_CODEC_RUNNER_APP_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/interface_request.h>
#include <src/lib/fxl/logging.h>

#include <memory>

#include "local_single_codec_factory.h"

// If a software can only provide an encoder or decoder, the other should be
// assigned NoAdapter in the template arguments, e.g.:
//   CodecRunnerApp<CodecAdapterFfmpeg, NoAdapter>
template <typename Decoder, typename Encoder>
class CodecRunnerApp {
 public:
  CodecRunnerApp()
      : loop_(&kAsyncLoopConfigAttachToThread),
        startup_context_(component::StartupContext::CreateFromStartupInfo()),
        codec_admission_control_(std::make_unique<CodecAdmissionControl>(loop_.dispatcher())) {}

  void Run() {
    zx_status_t status = syslog::InitLogger();
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to init syslog: %d", status);

    startup_context_->outgoing().deprecated_services()->AddService(
        fidl::InterfaceRequestHandler<fuchsia::mediacodec::CodecFactory>(
            [this, codec_admission_control = codec_admission_control_.get()](
                fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request) {
              // We RemoveService() near the end of the present lambda, so it
              // should be impossible to receive a second CodecFactory request.
              FXL_DCHECK(!codec_factory_);

              fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem;
              startup_context_->ConnectToEnvironmentService(sysmem.NewRequest());
              codec_factory_ = std::make_unique<LocalSingleCodecFactory<Decoder, Encoder>>(
                  loop_.dispatcher(), std::move(sysmem), std::move(request),
                  [this](std::unique_ptr<CodecImpl> created_codec_instance) {
                    // Own codec implementation and bind it.
                    codec_instance_ = std::move(created_codec_instance);
                    codec_instance_->BindAsync([this] {
                      // Drop codec implementation and close channel on
                      // error.
                      codec_instance_.reset();
                      // The codec_instance_ channel is the only reason for
                      // the isolate to exist.
                      loop_.Quit();
                    });
                    // Drop factory and close factory channel.
                    codec_factory_.reset();
                  },
                  codec_admission_control,
                  [this](zx_status_t error) {
                    // Drop factory and close factory channel on error.
                    codec_factory_.reset();
                    // The codec_instance_ channel is the only reason for
                    // the isolate to exist.  If codec_instance_ wasn't
                    // created via the codec_factory_ before this point,
                    // it'll never be created via codec_factory_.
                    if (!codec_instance_) {
                      loop_.Quit();
                    }
                  });
              // This runner only expects a single Local Codec Factory to ever
              // be requested.
              //
              // This call deletes the presently-running lambda, so nothing
              // after this call can use the lambda's captures, including the
              // "this" pointer implicitly.
              startup_context_->outgoing()
                  .deprecated_services()
                  ->RemoveService<fuchsia::mediacodec::CodecFactory>();
            }));

    loop_.Run();

    // Run the loop_.Shutdown() here (before ~CodecRunnerApp), so that any
    // pending tasks get deleted sooner rather than later.  The only pending
    // task we expect to potentially be deleted here is the task queued in
    // ~CodecImpl that does ~CodecAdmission and then ~zx::channel (even if the
    // task is just deleted and not run).  That task needs to run or be
    // deleted before ~CodecAdmissionControl.
    loop_.Shutdown();
  }

 private:
  async::Loop loop_;
  std::unique_ptr<component::StartupContext> startup_context_;
  std::unique_ptr<CodecAdmissionControl> codec_admission_control_;
  std::unique_ptr<LocalSingleCodecFactory<Decoder, Encoder>> codec_factory_;
  std::unique_ptr<CodecImpl> codec_instance_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_CODEC_RUNNER_APP_H_
