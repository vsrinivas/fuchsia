// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_RUNNER_APP_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_RUNNER_APP_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <initializer_list>
#include <memory>
#include <string>

#include "local_single_codec_factory.h"

// If a software can only provide an encoder or decoder, the other should be
// assigned NoAdapter in the template arguments, e.g.:
//   CodecRunnerApp<CodecAdapterFfmpeg, NoAdapter>
template <typename Decoder, typename Encoder>
class CodecRunnerApp {
 public:
  CodecRunnerApp()
      : codec_admission_control_(std::make_unique<CodecAdmissionControl>(loop_.dispatcher())) {}

  void Init() {
    static const std::initializer_list<std::string> kLogTags = {"vaapi_codec_runner"};
#ifdef NDEBUG
    syslog::SetTags(kLogTags);
#else
    syslog::SetLogSettings({.min_log_level = syslog::LOG_DEBUG}, kLogTags);
#endif

    trace_provider_ =
        std::make_unique<trace::TraceProviderWithFdio>(loop_.dispatcher(), "vaapi_codec_runner");

    // This is bit of a misnomer since CodecRunnerApp isn't a driver but instead a component. Since
    // we need a new instance for every FIDL connection a driver does not make sense currently.
    codec_diagnostics_ =
        std::make_unique<CodecDiagnostics>(component_context_, "vaapi_codec_runner");

    component_context_->outgoing()->AddPublicService(
        fidl::InterfaceRequestHandler<fuchsia::mediacodec::CodecFactory>(
            [this, codec_admission_control = codec_admission_control_.get()](
                fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request) {
              // We RemoveService() near the end of the present lambda, so it
              // should be impossible to receive a second CodecFactory request.
              FX_DCHECK(!codec_factory_);

              fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem;
              component_context_->svc()->Connect(sysmem.NewRequest());
              codec_factory_ = std::make_unique<LocalSingleCodecFactory<Decoder, Encoder>>(
                  loop_.dispatcher(), std::move(sysmem), std::move(request),
                  [this](std::unique_ptr<CodecImpl> created_codec_instance) {
                    ZX_DEBUG_ASSERT(!codec_instance_);
                    if (!created_codec_instance) {
                      // Drop factory and close factory channel on factory
                      // failure to create instance.
                      codec_factory_ = nullptr;
                      // The codec_instance_ channel is the only reason for the
                      // isolate to exist.
                      loop_.Quit();
                      return;
                    }
                    // Own codec implementation and bind it.
                    codec_instance_ = std::move(created_codec_instance);
                    codec_instance_->BindAsync([this] {
                      // Drop codec implementation and close channel on
                      // error.
                      codec_instance_ = nullptr;
                      // The codec_instance_ channel is the only reason for
                      // the isolate to exist.
                      loop_.Quit();
                    });
                    // Drop factory and close factory channel.
                    codec_factory_ = nullptr;
                  },
                  codec_admission_control,
                  [this](zx_status_t error) {
                    // Drop factory and close factory channel on error.
                    codec_factory_ = nullptr;
                    // The codec_instance_ channel is the only reason for
                    // the isolate to exist.  If codec_instance_ wasn't
                    // created via the codec_factory_ before this point,
                    // it'll never be created via codec_factory_.
                    if (!codec_instance_) {
                      loop_.Quit();
                    }
                  },
                  codec_diagnostics_.get());
              // This runner only expects a single Local Codec Factory to ever
              // be requested.
              //
              // This call deletes the presently-running lambda, so nothing
              // after this call can use the lambda's captures, including the
              // "this" pointer implicitly.
              component_context_->outgoing()
                  ->RemovePublicService<fuchsia::mediacodec::CodecFactory>();
            }));
  }

  void Serve() { component_context_->outgoing()->ServeFromStartupInfo(); }

  void Run() {
    loop_.Run();

    // Run the loop_.Shutdown() here (before ~CodecRunnerApp), so that any
    // pending tasks get deleted sooner rather than later.  The only pending
    // task we expect to potentially be deleted here is the task queued in
    // ~CodecImpl that does ~CodecAdmission and then ~zx::channel (even if the
    // task is just deleted and not run).  That task needs to run or be
    // deleted before ~CodecAdmissionControl.
    loop_.Shutdown();
  }

  std::unique_ptr<sys::ComponentContext>& component_context() { return component_context_; }

 private:
  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};
  std::unique_ptr<sys::ComponentContext> component_context_{sys::ComponentContext::Create()};
  std::unique_ptr<CodecAdmissionControl> codec_admission_control_;
  std::unique_ptr<LocalSingleCodecFactory<Decoder, Encoder>> codec_factory_;
  std::unique_ptr<CodecImpl> codec_instance_;
  std::unique_ptr<trace::TraceProviderWithFdio> trace_provider_;
  std::unique_ptr<CodecDiagnostics> codec_diagnostics_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_RUNNER_APP_H_
