// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <zircon/rights.h>
#include <zircon/syscalls/exception.h>

#include <fbl/ref_ptr.h>
#include <object/channel_dispatcher.h>
#include <object/handle.h>

#include "object/exceptionate.h"

namespace {

bool overwrite_valid_channel_fails() {
  BEGIN_TEST;

  KernelHandle<ChannelDispatcher> channels[4];
  zx_rights_t rights;
  ASSERT_EQ(ZX_OK, ChannelDispatcher::Create(&channels[0], &channels[1], &rights));
  ASSERT_EQ(ZX_OK, ChannelDispatcher::Create(&channels[2], &channels[3], &rights));

  Exceptionate exceptionate(ZX_EXCEPTION_CHANNEL_TYPE_THREAD);
  ASSERT_EQ(ZX_OK, exceptionate.SetChannel(ktl::move(channels[0]), 0, 0));

  EXPECT_EQ(ZX_ERR_ALREADY_BOUND, exceptionate.SetChannel(ktl::move(channels[2]), 0, 0));

  END_TEST;
}

bool overwrite_invalid_channel_succeeds() {
  BEGIN_TEST;

  KernelHandle<ChannelDispatcher> channels[4];
  zx_rights_t rights;
  ASSERT_EQ(ZX_OK, ChannelDispatcher::Create(&channels[0], &channels[1], &rights));
  ASSERT_EQ(ZX_OK, ChannelDispatcher::Create(&channels[2], &channels[3], &rights));

  Exceptionate exceptionate(ZX_EXCEPTION_CHANNEL_TYPE_THREAD);
  ASSERT_EQ(ZX_OK, exceptionate.SetChannel(ktl::move(channels[0]), 0, 0));

  channels[1].reset();
  EXPECT_EQ(ZX_OK, exceptionate.SetChannel(ktl::move(channels[2]), 0, 0));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(exception_tests)
UNITTEST("overwrite_valid_channel_fails", overwrite_valid_channel_fails)
UNITTEST("overwrite_invalid_channel_succeeds", overwrite_invalid_channel_succeeds)
UNITTEST_END_TESTCASE(exception_tests, "exception", "Exception tests")
