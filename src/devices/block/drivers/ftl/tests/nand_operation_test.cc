// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand_operation.h"

#include <fuchsia/hardware/nand/cpp/banjo.h>
#include <lib/fit/function.h>
#include <zircon/errors.h>

#include <iostream>

#include <zxtest/zxtest.h>

namespace {

TEST(NandOperationTest, TrivialLifetime) { ftl::NandOperation operation(sizeof(nand_operation_t)); }

TEST(NandOperationTest, SetDataVmo) {
  ftl::NandOperation operation(sizeof(nand_operation_t));

  nand_operation_t* op = operation.GetOperation();
  EXPECT_EQ(0, op->rw.data_vmo);

  ASSERT_OK(operation.SetDataVmo(55));

  EXPECT_NE(0, op->rw.data_vmo);
  EXPECT_NE(ZX_HANDLE_INVALID, op->rw.data_vmo);
  EXPECT_EQ(55, operation.buffer_size());
  EXPECT_NE(nullptr, operation.buffer());
}

TEST(NandOperationTest, SetOobVmo) {
  ftl::NandOperation operation(sizeof(nand_operation_t));

  nand_operation_t* op = operation.GetOperation();
  EXPECT_EQ(0, op->rw.oob_vmo);

  ASSERT_OK(operation.SetOobVmo(66));

  EXPECT_NE(0, op->rw.oob_vmo);
  EXPECT_NE(ZX_HANDLE_INVALID, op->rw.oob_vmo);
  EXPECT_EQ(66, operation.buffer_size());
  EXPECT_NE(nullptr, operation.buffer());
}

class NandTester : public ddk::NandProtocol<NandTester> {
 public:
  NandTester() : proto_({&nand_protocol_ops_, this}), doubler_(&proto_) {}

  ftl::OobDoubler* doubler() { return &doubler_; }
  nand_operation_t* operation() { return operation_; }

  void set_result_provider(fit::function<zx_status_t()> result_provider) {
    result_provider_ = std::move(result_provider);
  }

  void NandQuery(fuchsia_hardware_nand_Info* out_info, size_t* out_nand_op_size) {
    *out_info = {};
    *out_nand_op_size = 0;
  }

  void NandQueue(nand_operation_t* operation, nand_queue_callback callback, void* cookie) {
    operation_ = operation;
    auto result = result_provider_();
    callback(cookie, result, operation);
  }

  zx_status_t NandGetFactoryBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
                                         size_t* out_bad_blocks_actual) {
    return ZX_OK;
  }

 private:
  nand_protocol_t proto_;
  nand_operation_t* operation_;
  ftl::OobDoubler doubler_;
  fit::function<zx_status_t()> result_provider_ = []() { return ZX_OK; };
};

TEST(NandOperationTest, ExecuteSuccess) {
  ftl::NandOperation operation(sizeof(nand_operation_t));
  nand_operation_t* op = operation.GetOperation();

  NandTester tester;
  ASSERT_OK(operation.Execute(tester.doubler()));

  EXPECT_EQ(op, tester.operation());
}

TEST(NandOperationTest, ExecuteFailure) {
  ftl::NandOperation operation(sizeof(nand_operation_t));
  nand_operation_t* op = operation.GetOperation();

  NandTester tester;
  tester.set_result_provider([]() { return ZX_HANDLE_INVALID; });
  ASSERT_EQ(ZX_HANDLE_INVALID, operation.Execute(tester.doubler()));

  EXPECT_EQ(op, tester.operation());
}

TEST(NandOperationTest, ExecuteBatchSuccess) {
  std::vector<std::unique_ptr<ftl::NandOperation>> operations(20);
  for (auto& operation : operations) {
    operation = std::make_unique<ftl::NandOperation>(sizeof(nand_operation_t));
  }

  NandTester tester;
  auto results = ftl::NandOperation::ExecuteBatch(tester.doubler(), operations);

  for (auto result : results) {
    EXPECT_TRUE(result.is_ok());
  }
}

TEST(NandOperationTest, ExecuteBatchSuccessAndFailures) {
  std::vector<std::unique_ptr<ftl::NandOperation>> operations(20);
  for (auto& operation : operations) {
    operation = std::make_unique<ftl::NandOperation>(sizeof(nand_operation_t));
  }

  NandTester tester;
  int operation_count = 0;
  // Even operations are failed.
  tester.set_result_provider([&]() -> zx_status_t {
    if (operation_count++ % 2 == 0) {
      return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
  });

  auto results = ftl::NandOperation::ExecuteBatch(tester.doubler(), operations);

  int i = 0;
  for (auto result : results) {
    if (i++ % 2 == 0) {
      EXPECT_TRUE(result.is_error());
    } else {
      EXPECT_TRUE(result.is_ok());
    }
  }
}

}  // namespace
