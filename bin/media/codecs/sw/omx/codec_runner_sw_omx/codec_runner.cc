// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_runner.h"

#include <lib/async/cpp/task.h>

namespace codec_runner {

CodecRunner::CodecRunner(async_dispatcher_t* fidl_dispatcher,
                         thrd_t fidl_thread)
    : fidl_dispatcher_(fidl_dispatcher), fidl_thread_(fidl_thread) {
  // nothing else to do here
}

CodecRunner::~CodecRunner() {
  // nothing to do here
}

void CodecRunner::BindAndOwnSelf(
    fidl::InterfaceRequest<fuchsia::mediacodec::Codec> codec_request,
    std::unique_ptr<CodecRunner> self) {
  assert(thrd_current() == fidl_thread_);
  // We have input_constraints_ by now thanks to our behavior (server-side),
  // so this can be an assert().
  assert(input_constraints_);

  binding_ = std::make_unique<BindingType>(std::move(self));
  binding_->set_error_handler([this] {
    // No point in trying to send an epitaph here since the reason we're here
    // is the other end being gone.
    //
    // This class is only used for running one Codec instance per process.
    //
    // Since the channel failed, the client probably won't see this message.
    Exit("The Codec channel failed server-side.  Normal if client is done.");
  });
  binding_->Bind(std::move(codec_request), fidl_dispatcher_);

  // Some sub-classes already want to convey some output constraints as early
  // as possible - this is a place for those sub-classes to do so.  Sending
  // before input constraints encourages the client to configure output before
  // delivering input that starts the first stream, to try to avoid extra
  // output re-configs.
  onInputConstraintsReady();

  // Now we can tell the client about the input constraints.  We do this as an
  // event because the client has no choice re. whether the client needs
  // these. These are _always_ needed by the client.  Also, as an event it
  // would be easier to have the CodecFactory potentially send these instead
  // of the Codec to save a bit on latency.
  //
  // Intentional copy, in case a derived class wants to refer to
  // input_constraints_.
  //
  // TODO(dustingreen): Make these serial, make the serial context be the same
  // one and be visible to all the places that need to send, probably serial
  // context in CodecRunner as a protected field.  OR, ask and confirm that
  // async::PostTask() is guaranteed to remain serial (like it was before, and
  // like it seems to be the vast majority of the time currently).
  input_constraints_sent_ = true;

  // We post here so that we're ordered after similar posting done in
  // onInputConstraintsReady() above, so that the derived class has every chance
  // to send output constraints before input constraints to encourage client to
  // configure output before starting to deliver input data.
  async::PostTask(fidl_dispatcher_, [this] {
    binding_->events().OnInputConstraints(*input_constraints_);
  });

  onSetupDone();
}

void CodecRunner::Exit(const char* format, ...) {
  // TODO(dustingreen): Send epitaph when possible.

  // Let's not have a buffer on the stack, not because it couldn't be done
  // safely, but because we'd potentially run into stack size vs. message length
  // tradeoffs, stack expansion granularity fun, or whatever else.

  va_list args;
  va_start(args, format);
  size_t buffer_bytes = vsnprintf(nullptr, 0, format, args) + 1;
  va_end(args);

  // ~buffer never actually runs since this method never returns
  std::unique_ptr<char[]> buffer(new char[buffer_bytes]);

  va_start(args, format);
  size_t buffer_bytes_2 =
      vsnprintf(buffer.get(), buffer_bytes, format, args) + 1;
  (void)buffer_bytes_2;
  // sanity check; should match so go ahead and assert that it does.
  assert(buffer_bytes == buffer_bytes_2);
  va_end(args);

  // TODO(dustingreen): It might be worth wiring this up to the log in a more
  // official way, especially if doing so would print a timestamp automatically
  // and/or provide filtering goodness etc.
  printf("%s  --  Codec server isolate will exit(-1)\n", buffer.get());

  // TODO(dustingreen): Send string in buffer via epitaph, when possible.  First
  // we should switch to events so we'll only have the Codec channel not the
  // CodecEvents channel. Note to self: The channel failing server-side may race
  // with trying to send.

  // TODO(dustingreen): determine if our heap leak detection will be able to
  // tolerate this exit(-1) and still detect leaks - and fix it to tolerate if
  // it doesn't already, because it should.

  exit(-1);
}

}  // namespace codec_runner
