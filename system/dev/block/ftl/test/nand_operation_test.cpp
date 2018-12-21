// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand_operation.h"

#include <ddktl/protocol/nand.h>
#include <unittest/unittest.h>

namespace {

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    ftl::NandOperation operation(sizeof(nand_operation_t));
    END_TEST;
}

bool SetDataVmoTest() {
    BEGIN_TEST;
    ftl::NandOperation operation(sizeof(nand_operation_t));

    nand_operation_t* op = operation.GetOperation();
    EXPECT_EQ(0, op->rw.data_vmo);

    ASSERT_EQ(ZX_OK, operation.SetDataVmo(55));

    EXPECT_NE(0, op->rw.data_vmo);
    EXPECT_NE(ZX_HANDLE_INVALID, op->rw.data_vmo);
    EXPECT_EQ(55, operation.buffer_size());
    EXPECT_NE(nullptr, operation.buffer());
    END_TEST;
}

bool SetOobVmoTest() {
    BEGIN_TEST;
    ftl::NandOperation operation(sizeof(nand_operation_t));

    nand_operation_t* op = operation.GetOperation();
    EXPECT_EQ(0, op->rw.oob_vmo);

    ASSERT_EQ(ZX_OK, operation.SetOobVmo(66));

    EXPECT_NE(0, op->rw.oob_vmo);
    EXPECT_NE(ZX_HANDLE_INVALID, op->rw.oob_vmo);
    EXPECT_EQ(66, operation.buffer_size());
    EXPECT_NE(nullptr, operation.buffer());
    END_TEST;
}

class NandTester : public ddk::NandProtocol<NandTester> {
  public:
    NandTester() : proto_({&nand_protocol_ops_, this}), doubler_(&proto_, false) {}

    ftl::OobDoubler* doubler() { return &doubler_; }
    nand_operation_t* operation() { return operation_; }

    void set_result(zx_status_t result) { result_ = result; }

    void NandQuery(zircon_nand_Info* out_info, size_t* out_nand_op_size) {
        *out_info = {};
        *out_nand_op_size = 0;
    }

    void NandQueue(nand_operation_t* operation, nand_queue_callback callback, void* cookie) {
        operation_ = operation;
        callback(cookie, result_, operation);
    }

    zx_status_t NandGetFactoryBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
                                           size_t* out_bad_blocks_actual) {
      return ZX_OK;
    }

  private:
    nand_protocol_t proto_;
    nand_operation_t* operation_;
    ftl::OobDoubler doubler_;
    zx_status_t result_ = ZX_OK;
};

bool ExecuteSuccessTest() {
    BEGIN_TEST;
    ftl::NandOperation operation(sizeof(nand_operation_t));
    nand_operation_t* op = operation.GetOperation();

    NandTester tester;
    ASSERT_EQ(ZX_OK, operation.Execute(tester.doubler()));

    EXPECT_EQ(op, tester.operation());
    END_TEST;
}

bool ExecuteFailureTest() {
    BEGIN_TEST;
    ftl::NandOperation operation(sizeof(nand_operation_t));
    nand_operation_t* op = operation.GetOperation();

    NandTester tester;
    tester.set_result(ZX_HANDLE_INVALID);
    ASSERT_EQ(ZX_HANDLE_INVALID, operation.Execute(tester.doubler()));

    EXPECT_EQ(op, tester.operation());
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(NandOperationTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(SetDataVmoTest)
RUN_TEST_SMALL(SetOobVmoTest)
RUN_TEST_SMALL(ExecuteSuccessTest)
RUN_TEST_SMALL(ExecuteFailureTest)
END_TEST_CASE(NandOperationTests)
