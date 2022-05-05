// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async/sequence_id.h>

#include <zxtest/zxtest.h>

namespace {

class FakeSequenceIdAsync : public async::DispatcherStub {
 public:
  zx_status_t GetSequenceId(async_sequence_id_t* out_sequence_id) override {
    *out_sequence_id = current_sequence_id_;
    return ZX_OK;
  }

  void SetSequenceId(async_sequence_id_t current_sequence_id) {
    current_sequence_id_ = current_sequence_id;
  }

 private:
  async_sequence_id_t current_sequence_id_ = {};
};

TEST(SequenceIdTests, GetSequenceId) {
  FakeSequenceIdAsync dispatcher;

  dispatcher.SetSequenceId({.value = 0});
  async_sequence_id_t sequence_id = {};
  EXPECT_OK(async_get_sequence_id(&dispatcher, &sequence_id));
  EXPECT_EQ(0, sequence_id.value);

  dispatcher.SetSequenceId({.value = 42});
  sequence_id = {};
  EXPECT_OK(async_get_sequence_id(&dispatcher, &sequence_id));
  EXPECT_EQ(42, sequence_id.value);
}

}  // namespace
