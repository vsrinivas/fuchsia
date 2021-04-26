// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/syscall_decoder_interface.h"

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

SyscallDecoderInterface::SyscallDecoderInterface(SyscallDecoderDispatcher* dispatcher,
                                                 zxdb::Thread* thread)
    : dispatcher_(dispatcher),
      arch_(thread->session()->arch()),
      fidlcat_thread_(dispatcher->SearchThread(thread->GetKoid())) {}

}  // namespace fidlcat
