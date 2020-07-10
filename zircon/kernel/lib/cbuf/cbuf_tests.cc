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
  {
    Cbuf cbuf;
  }

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
    zx::status<char> result = cbuf.ReadChar(true);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value(), c);
  }
  ASSERT_FALSE(cbuf.Full());

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(cbuf_tests)
UNITTEST("Constructor", Constructor)
UNITTEST("ReadWrite", ReadWrite)
UNITTEST_END_TESTCASE(cbuf_tests, "cbuf", "cbuf tests")
