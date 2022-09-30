// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async/sequence_id.h>

#include <zxtest/zxtest.h>

namespace {

class FakeSequenceIdAsync : public async::DispatcherStub {
 public:
  zx_status_t GetSequenceId(async_sequence_id_t* out_sequence_id, const char** out_error) override {
    *out_sequence_id = current_sequence_id_;
    return ZX_OK;
  }

  void SetSequenceId(async_sequence_id_t current_sequence_id) {
    current_sequence_id_ = current_sequence_id;
  }

  zx_status_t CheckSequenceId(async_sequence_id_t sequence_id, const char** out_error) override {
    if (current_sequence_id_.value == sequence_id.value) {
      return ZX_OK;
    }
    *out_error = "wrong";
    return ZX_ERR_OUT_OF_RANGE;
  }

 private:
  async_sequence_id_t current_sequence_id_ = {};
};

TEST(SequenceIdTests, GetSequenceId) {
  FakeSequenceIdAsync dispatcher;

  dispatcher.SetSequenceId({.value = 0});
  async_sequence_id_t sequence_id = {};
  const char* error = nullptr;
  EXPECT_OK(async_get_sequence_id(&dispatcher, &sequence_id, &error));
  EXPECT_EQ(0, sequence_id.value);
  EXPECT_NULL(error);

  dispatcher.SetSequenceId({.value = 42});
  sequence_id = {};
  error = nullptr;
  EXPECT_OK(async_get_sequence_id(&dispatcher, &sequence_id, &error));
  EXPECT_EQ(42, sequence_id.value);
  EXPECT_NULL(error);
}

TEST(SequenceIdTests, CheckSequenceId) {
  FakeSequenceIdAsync dispatcher;

  dispatcher.SetSequenceId({.value = 0});
  async_sequence_id_t sequence_id = {};
  const char* error = nullptr;
  EXPECT_OK(async_check_sequence_id(&dispatcher, sequence_id, &error));
  EXPECT_NULL(error);

  dispatcher.SetSequenceId({.value = 1});
  sequence_id = {};
  error = nullptr;
  EXPECT_STATUS(ZX_ERR_OUT_OF_RANGE, async_check_sequence_id(&dispatcher, sequence_id, &error));
  EXPECT_STREQ("wrong", error);
}

TEST(SequenceIdTests, Unsupported) {
  constexpr const static async_ops_t kOps = {
      .version = ASYNC_OPS_V1,
      .reserved = 0,
  };
  async_dispatcher_t dispatcher{
      .ops = &kOps,
  };
  async_sequence_id_t sequence_id = {};
  const char* error = nullptr;
  EXPECT_STATUS(ZX_ERR_NOT_SUPPORTED, async_get_sequence_id(&dispatcher, &sequence_id, &error));
  EXPECT_SUBSTR(error, "support");

  sequence_id = {};
  error = nullptr;
  EXPECT_STATUS(ZX_ERR_NOT_SUPPORTED, async_check_sequence_id(&dispatcher, sequence_id, &error));
  EXPECT_SUBSTR(error, "support");
}

}  // namespace
