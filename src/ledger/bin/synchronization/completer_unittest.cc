// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/synchronization/completer.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {

using CompleterTest = ledger::TestWithEnvironment;

TEST_F(CompleterTest, TasksAreQueued) {
  Completer completer(environment_.dispatcher());

  EXPECT_FALSE(completer.IsCompleted());

  Status status_1;
  bool task_1_has_run;
  completer.WaitUntilDone(callback::Capture(callback::SetWhenCalled(&task_1_has_run), &status_1));

  Status status_2;
  bool task_2_has_run;
  completer.WaitUntilDone(callback::Capture(callback::SetWhenCalled(&task_2_has_run), &status_2));
  RunLoopUntilIdle();

  EXPECT_FALSE(task_1_has_run);
  EXPECT_FALSE(task_2_has_run);

  completer.Complete(Status::IO_ERROR);
  // Verify that tasks are not executed synchronously.
  EXPECT_FALSE(task_1_has_run);
  EXPECT_FALSE(task_2_has_run);
  RunLoopUntilIdle();

  EXPECT_TRUE(completer.IsCompleted());

  EXPECT_TRUE(task_1_has_run);
  EXPECT_EQ(status_1, Status::IO_ERROR);
  EXPECT_TRUE(task_2_has_run);
  EXPECT_EQ(status_2, Status::IO_ERROR);

  Status status_3;
  bool task_3_has_run;
  completer.WaitUntilDone(callback::Capture(callback::SetWhenCalled(&task_3_has_run), &status_3));
  RunLoopUntilIdle();
  EXPECT_TRUE(task_3_has_run);
  EXPECT_EQ(status_3, Status::IO_ERROR);
}

TEST_F(CompleterTest, SyncWait) {
  Completer completer(environment_.dispatcher());

  EXPECT_FALSE(completer.IsCompleted());

  Status status_1 = Status::NOT_IMPLEMENTED;
  bool task_1_has_run = false;
  environment_.coroutine_service()->StartCoroutine([&](coroutine::CoroutineHandler* handler) {
    status_1 = SyncWaitUntilDone(handler, &completer);
    task_1_has_run = true;
  });

  EXPECT_FALSE(task_1_has_run);

  completer.Complete(Status::IO_ERROR);
  RunLoopUntilIdle();
  EXPECT_TRUE(task_1_has_run);
  EXPECT_EQ(status_1, Status::IO_ERROR);
}

}  // namespace ledger
