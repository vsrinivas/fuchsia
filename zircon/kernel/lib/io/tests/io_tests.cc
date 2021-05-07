// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/io.h>
#include <lib/unittest/unittest.h>
#include <lib/zircon-internal/macros.h>

#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <kernel/thread_lock.h>

namespace {

// Call serial_write while holding the thread lock to establish a lock ordering between the thread
// lock and the uart_serial lock.  By establishing the lock ordering, lockdep may be able to detect
// violations of this ordering.  This is a regression test for fxbug.dev/76022.
bool SerialWriteHoldingThreadLockTest() {
  BEGIN_TEST;

  Guard<MonitoredSpinLock, IrqSave> thread_lock_guard{ThreadLock::Get(), SOURCE_TAG};
  serial_write("this is a test message from SerialWriteHoldingThreadLockTest\n");

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(io_tests)
UNITTEST("serial_write_holding_thread_lock", SerialWriteHoldingThreadLockTest)
UNITTEST_END_TESTCASE(io_tests, "io_tests", "io test")
