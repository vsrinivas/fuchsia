// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_runner_component.h"

#include "local_codec_factory.h"

namespace {

// We may in future allow creation strategies that involve sharing a process
// across more than one Codec instance, but for now we don't, so enforce max of
// one CodecFactory instance ever in this process.
bool is_factory_created = false;

}  // namespace

namespace codec_runner {

CodecRunnerComponent::CodecRunnerComponent(
    async_dispatcher_t* fidl_dispatcher, thrd_t fidl_thread,
    std::unique_ptr<fuchsia::sys::StartupContext> startup_context)
    : fidl_dispatcher_(fidl_dispatcher),
      fidl_thread_(fidl_thread),
      startup_context_(std::move(startup_context)) {
  startup_context_->outgoing_services()->AddServiceForName(
      [this](zx::channel request) {
        // This process only intends to have up to one CodecFactory at least for
        // now, so enforce that here.
        if (is_factory_created) {
          // TODO: send epitaph, when possible
          request.reset();
          assert(!is_factory_created);
          exit(-1);
        }
        // We use the self-owned pattern rather than a singleton, in case we
        // later allow more than one, since the CodecFactory interface is
        // stateful by design.
        codec_factory::LocalCodecFactory::CreateSelfOwned(
            fidl_dispatcher_, fidl_thread_,
            fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory>(
                std::move(request)));
      },
      fuchsia::mediacodec::CodecFactory::Name_);
}

}  // namespace codec_runner
