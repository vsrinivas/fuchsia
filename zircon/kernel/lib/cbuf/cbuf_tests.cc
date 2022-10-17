// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cbuf.h>
#include <lib/unittest/unittest.h>
#include <zircon/errors.h>

namespace {

bool Constructor() {
  BEGIN_TEST;

  // Construct, but don't initialize.
  { Cbuf cbuf; }

  // Construct and initialize.
  {
    char buffer[32];
    Cbuf cbuf;
    cbuf.Initialize(sizeof(buffer), buffer);
    ASSERT_FALSE(cbuf.Full());
  }

  END_TEST;
}

bool ReadWrite() {
  BEGIN_TEST;

  char buffer[4];
  Cbuf cbuf;
  cbuf.Initialize(sizeof(buffer), buffer);

  // Nothing to read, don't wait.
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, cbuf.ReadChar(false).status_value());

  // Write some characters.
  char data[] = {'A', 'B', 'C'};
  for (char c : data) {
    ASSERT_EQ(1U, cbuf.WriteChar(c));
  }
  ASSERT_TRUE(cbuf.Full());

  // Read them back.
  for (char c : data) {
    zx::result<char> result = cbuf.ReadChar(true);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value(), c);
  }
  ASSERT_FALSE(cbuf.Full());

  END_TEST;
}

// This test verifies that a thread repeatedly calling ReadChar concurrently with another thread
// calling WriteChar can be cleanly killed.  This is a regression test for fxbug.dev/76610.  It has
// no false positives (i.e. it should never spuriously fail), but it can have false negatives.
bool ReadWriteRace() {
  BEGIN_TEST;

  char buffer[4];
  Cbuf cbuf;
  cbuf.Initialize(sizeof(buffer), buffer);

  // Define a thread that will repeatedly read from the Cbuf until it is killed.
  thread_start_routine fn = [](void* arg) -> int {
    Cbuf& cbuf = *reinterpret_cast<Cbuf*>(arg);
    while (true) {
      zx::result<char> result = cbuf.ReadChar(true);
      if (!result.is_ok()) {
        return result.error_value();
      }
    }
  };

  // Create and start the thread.
  Thread* t = Thread::Create("cbuf race", fn, &cbuf, DEFAULT_PRIORITY);
  ASSERT_TRUE(t != nullptr);
  t->Resume();

  // The number of loop iterations should be large enough to create an opportunity for ReadChar and
  // WriteChar to race, but small enough to ensure the test completes quickly.
  for (int i = 0; i < 1000; ++i) {
    cbuf.WriteChar('A');
  }

  // Kill the thread and wait for it to terminate.
  t->Kill();
  int retcode;
  t->Join(&retcode, ZX_TIME_INFINITE);
  ASSERT_EQ(ZX_ERR_INTERNAL_INTR_KILLED, retcode);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(cbuf_tests)
UNITTEST("Constructor", Constructor)
UNITTEST("ReadWrite", ReadWrite)
UNITTEST("ReadWriteRace", ReadWriteRace)
UNITTEST_END_TESTCASE(cbuf_tests, "cbuf", "cbuf tests")
