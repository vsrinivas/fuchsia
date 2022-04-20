// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sequence/get_id.h>
#include <lib/sequence/set_id.h>

#include <thread>

#include <zxtest/zxtest.h>

TEST(Sequence, DefaultForNewThread) {
  std::thread t([] { EXPECT_EQ(SEQUENCE_ID_NONE, sequence_id_get()); });
  t.join();
}

TEST(Sequence, SetOnCurrentThread) {
  sequence_id_t id = 42;
  sequence_id_set(id);
  EXPECT_EQ(id, sequence_id_get());
}

TEST(Sequence, GetOnOtherThread) {
  sequence_id_t id = 42;
  sequence_id_set(id);
  std::thread t([] { EXPECT_EQ(SEQUENCE_ID_NONE, sequence_id_get()); });
  t.join();
}
