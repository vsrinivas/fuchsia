// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_runner_app.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/logging.h>

CodecRunnerApp::CodecRunnerApp()
    : loop_(&kAsyncLoopConfigAttachToThread),
      startup_context_(component::StartupContext::CreateFromStartupInfo()) {}

void CodecRunnerApp::Run() {
  // Lives until after Loop.Run() which is fine since it is not referenced after
  // Loop.Run().
  auto codec_admission_control =
      std::make_unique<CodecAdmissionControl>(loop_.dispatcher());

  startup_context_->outgoing().deprecated_services()->AddService(
      fidl::InterfaceRequestHandler<fuchsia::mediacodec::CodecFactory>(
          [this, codec_admission_control = codec_admission_control.get()](
              fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory>
                  request) {
            // This runner only expects a single Local Codec Factory to ever be
            // requested.
            FXL_DCHECK(!codec_factory_);
            startup_context_->outgoing().deprecated_services()
                ->RemoveService<fuchsia::mediacodec::CodecFactory>();

            codec_factory_.reset(new LocalSingleCodecFactory(
                loop_.dispatcher(), std::move(request),
                [this](std::unique_ptr<CodecImpl> created_codec_instance) {
                  // Own codec implementation and bind it.
                  codec_instance_ = std::move(created_codec_instance);
                  codec_instance_->BindAsync([this] {
                    // Drop codec implementation and close channel on error.
                    codec_instance_.reset();
                  });
                  // Drop factory and close factory channel.
                  codec_factory_.reset();
                },
                codec_admission_control,
                [this](zx_status_t error) {
                  // Drop factory and close factory channel on error.
                  codec_factory_.reset();
                }));
          }));

  loop_.Run();
}
