// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _APPS_LEDGER_SRC_TEST_TEST_WITH_COROUTINES_H_
#define _APPS_LEDGER_SRC_TEST_TEST_WITH_COROUTINES_H_

#include "apps/ledger/src/coroutine/coroutine_impl.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "garnet/public/lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace test {

class TestWithCoroutines : public TestWithMessageLoop {
 public:
  TestWithCoroutines();

 protected:
  // Runs the given the given test code in a coroutine. Returns |true| if the
  // test has successfully termitated.
  bool RunInCoroutine(
      std::function<void(coroutine::CoroutineHandler*)> run_test);

  coroutine::CoroutineServiceImpl coroutine_service_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(TestWithCoroutines);
};

}  // namespace test

#endif  // _APPS_LEDGER_SRC_TEST_TEST_WITH_COROUTINES_H_
