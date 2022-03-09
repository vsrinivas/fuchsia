// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <perftest/perftest.h>

#include "main.h"
#include "src/lib/files/path.h"

namespace {

// Measure the time taken to launch (and then wait for) a process that
// simply exits, where the process is launched using fork+exec.
//
// Note that the time taken by fork() may tend to depend on the size
// of the address space of the parent process (i.e. the number of
// mappings), because fork() takes a snapshot of that address space.
bool SpawnTest(perftest::RepeatState* state) {
  std::string parent_dir = files::GetDirectoryName(argv0);
  std::string executable = files::JoinPath(parent_dir, "fdio_spawn_helper");
  const char* executable_str = executable.c_str();
  const char* argv[] = {executable_str, nullptr};

  while (state->KeepRunning()) {
    int pid = fork();
    ZX_ASSERT(pid >= 0);
    if (pid == 0) {
      // In child process.
      execv(executable_str, const_cast<char**>(argv));
      fprintf(stderr, "exec failed: %s: %s\n", executable_str, strerror(errno));
      _exit(1);
    }
    // In parent process.
    int status = 0;
    int pid2 = waitpid(pid, &status, 0);
    ZX_ASSERT(pid2 >= 0);
    ZX_ASSERT(pid2 == pid);
    ZX_ASSERT(WIFEXITED(status));
    ZX_ASSERT(WEXITSTATUS(status) == 0);
  }
  return true;
}

void RegisterTests() { perftest::RegisterTest("ProcessSpawn", SpawnTest); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
