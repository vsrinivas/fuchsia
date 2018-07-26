// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/time.h>

#include "lib/fidl/cpp/binding.h"

#include "codec_runner_component.h"

// For now, this executable only knows about OMX .so libs (essentially as data
// deps), and won't load any others.
//
// The .so interface used between this executable and OMX .so libs is not part
// of the OMX standard, but it does stick to OMX C interfaces for the most part.
//
// The AOSP OMX codecs are just a convenient set of codecs to use as proof of
// concept.  The CodecFactory and Codec interfaces are more relevant system-wide
// than the OMX interfaces.  The OMX interfaces are used only in this
// executable.
//
// This executable serves up to one CodecFactory instance, only as a secondary
// implementation, with many assumptions re. the main CodecFactory's way of
// calling the secondary CodecFactory.  This process's CodecFactory interface is
// only served to the main CodecFactory, not to the client of the main
// CodecFactory.
//
// This executable's CodecFactory is used by the main CodecFactory
// implementation to create up to one Codec instance which is directly served in
// the local process, backed by an OMX codec instance, and served to the end
// client of the main CodecFactory.  For this reason, in contrast to the
// CodecFactory implementation which can make some simplifying interface usage
// assumptions, the Codec interface served by this process must be complete.

void usage(char* binary_name) { printf("usage: %s\n", binary_name); }

int main(int argc, char* argv[]) {
  if (argc != 1) {
    usage(argv[0]);
    exit(-1);
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  async::Now(loop.dispatcher());

  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  codec_runner::CodecRunnerComponent codec_runner(
      loop.dispatcher(), thrd_current(), std::move(startup_context));

  loop.Run();

  return 0;
}
