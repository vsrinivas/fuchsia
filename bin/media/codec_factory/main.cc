// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <trace-provider/provider.h>

#include "codec_factory_impl.h"

#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/svc/cpp/services.h"

namespace codec_factory {

// CodecFactoryApp is singleton per-process.
class CodecFactoryApp {
 public:
  CodecFactoryApp(std::unique_ptr<fuchsia::sys::StartupContext> startup_context,
                  async::Loop* loop);

 private:
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;

  // We don't keep a fidl::BindingSet<> here, as we want each CodecFactory
  // instance to be able to shoot down its own channel if an error occurs.
  // The App layer is just here to create CodecFactory instances, each
  // independently bound to its own channel using a std::unique_ptr ImplPtr so
  // that if the channel closes, the CodecFactory instance will go away.  And
  // if the CodecFactory instance wants to self-destruct, it can delete the
  // binding, which will close the channel and delete the CodecFactory.
  // This is true despite comments in the binding code that constantly say how
  // ImplPtr isn't taking ownership; as long as we use std::unique_ptr as
  // ImplPtr it actaully willl take ownership.
  //
  // We create a new instance of CodecFactory for each interface request,
  // because CodecFactory's implementation isn't stateless, by design, for
  // more plausible interface evolution over time.

  FXL_DISALLOW_COPY_AND_ASSIGN(CodecFactoryApp);
};

CodecFactoryApp::CodecFactoryApp(
    std::unique_ptr<fuchsia::sys::StartupContext> startup_context,
    async::Loop* loop)
    : startup_context_(std::move(startup_context)) {
  // TODO(dustingreen): Determine if this is useful and if we're holding it
  // right.
  trace::TraceProvider trace_provider(loop->async());

  startup_context_->outgoing_services()->AddServiceForName(
      [this](zx::channel request) {
        // The CodecFactoryImpl is self-owned and will self-delete when the
        // channel closes or an error occurs.
        CodecFactoryImpl::CreateSelfOwned(startup_context_.get(),
                                          std::move(request));
      },
      fuchsia::mediacodec::CodecFactory::Name_);
}

}  // namespace codec_factory

int main(int argc, char* argv[]) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  codec_factory::CodecFactoryApp app(
      fuchsia::sys::StartupContext::CreateFromStartupInfo(), &loop);

  loop.Run();

  return 0;
}
