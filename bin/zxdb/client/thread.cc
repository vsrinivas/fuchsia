// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread.h"

#include <sstream>

#include "garnet/bin/zxdb/client/process.h"
//#include "garnet/public/lib/fxl/logging.h"
//#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

Thread::Thread(Process* process, size_t thread_id, uint64_t koid)
    : ClientObject(process->session()),
      process_(process),
      thread_id_(thread_id),
      koid_(koid) {}
Thread::~Thread() {}

}  // namespace zxdb
