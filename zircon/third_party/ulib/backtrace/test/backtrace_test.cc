// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/processargs.h>

#include <map>
#include <string>
#include <vector>

#include <backtrace/backtrace.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

// A generic "backtrace_error_callback" implementation that fails the test.
void failing_error_callback(void* data, const char* msg, int errnum) {
  fprintf(stderr, "%s", msg);
  if (errnum > 0)
    fprintf(stderr, ": %s", strerror(errnum));
  fprintf(stderr, "\n");
  exit(EXIT_FAILURE);
}

// A simple test that simply exercises the backtrace code and ensures we get a callback.
TEST(Backtrace, SimpleBacktrace) {
  backtrace_state* state =
      backtrace_create_state(nullptr, /*threaded=*/0, failing_error_callback, nullptr);
  ASSERT_TRUE(state != nullptr);

  // Produce a backtrace.
  auto callback = +[](void* data, uintptr_t pc) {
    printf("  pc = %lu\n", pc);
    ((std::vector<uintptr_t>*)data)->push_back(pc);
    return 0;
  };
  std::vector<uintptr_t> pc_list;
  backtrace_simple(state, /*skip=*/0, callback, failing_error_callback, &pc_list);

  // Ensure at least 1 element was in the backtrace.
  EXPECT_GE(pc_list.size(), 1);

  // Clean up.
  backtrace_destroy_state(state, failing_error_callback, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  return RUN_ALL_TESTS(argc, argv);
}
