// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <trace-test-utils/fixture.h>
#include <zxtest/zxtest.h>

// N.B. This header has a sibling in //zircon/system/utest/trace, which
// contains significantly more functionality as it tests multiple situations,
// and in particular that the APIs work for both C and C++ clients. Although
// we're only concerned with the C++ case, the fixture itself doesn't export
// the Fixture type itself to make use of RAII. As such, while these macros
// are simplified to only consider a C++ case, they don't do so in a C++y way.

#define DEFAULT_BUFFER_SIZE_BYTES (1024u * 1024u)

// This isn't a do-while because of the cleanup.
#define BEGIN_TRACE_TEST_ETC(attach_to_thread, mode, buffer_size) \
  __attribute__((cleanup(fixture_scope_cleanup))) bool __scope;   \
  (void)__scope;                                                  \
  fixture_set_up((attach_to_thread), (mode), (buffer_size))

#define BEGIN_TRACE_TEST \
  BEGIN_TRACE_TEST_ETC(kAttachToThread, TRACE_BUFFERING_MODE_ONESHOT, DEFAULT_BUFFER_SIZE_BYTES)

#define ASSERT_RECORDS(expected) \
  ASSERT_TRUE(fixture_compare_records(expected), "record mismatch")
