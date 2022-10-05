// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/wait.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include "piped-command.h"

namespace zxdump {

fit::result<std::string> PipedCommand::StartArgv(cpp20::span<const char*> argv) {
  ZX_DEBUG_ASSERT(pid_ == -1);

  pid_ = fork();
  if (pid_ < 0) {
    return fit::error{strerror(errno)};
  }
  if (pid_ > 0) {
    return fit::ok();
  }

  for (auto& [number, fd] : redirect_) {
    dup2(std::exchange(fd, {}).get(), number);
  }
  execvp(argv[0], const_cast<char* const*>(argv.data()));
  fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
  _exit(127);
}

PipedCommand::~PipedCommand() {
  if (pid_ != -1) {
    waitpid(pid_, nullptr, 0);
  }
}

}  // namespace zxdump
