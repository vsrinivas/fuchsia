// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand.h"

#include <atomic>
#include <memory>
#include <utility>

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <unittest/unittest.h>

namespace {

constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kOobSize = 8;
constexpr uint32_t kNumPages = 20;
constexpr uint32_t kNumBlocks = 10;
constexpr uint32_t kEccBits = 10;
constexpr uint32_t kNumOobSize = 8;

constexpr uint8_t kMagic = 'd';
constexpr uint8_t kOobMagic = 'o';

fuchsia_hardware_nand_Info kInfo = {kPageSize, kNumPages, kNumBlocks, kEccBits, kNumOobSize, 0, {}};

enum class OperationType {
    kRead,
    kWrite,
    kErase,
};

struct LastOperation {
    OperationType type;
    uint32_t nandpage;
};

// Fake for the raw nand protocol.
class FakeRawNand : public ddk::RawNandProtocol<FakeRawNand> {
public:
    FakeRawNand()
        : proto_({&raw_nand_protocol_ops_, this}) {}

    const raw_nand_protocol_t* proto() const { return &proto_; }

    void set_result(zx_status_t result) { result_ = result; }
    void set_ecc_bits(uint32_t ecc_bits) { ecc_bits_ = ecc_bits; }

    // Raw nand protocol:
    zx_status_t RawNandGetNandInfo(fuchsia_hardware_nand_Info* out_info) {
        *out_info = info_;
        return result_;
    }

    zx_status_t RawNandReadPageHwecc(uint32_t nandpage, void* out_data_buffer, size_t data_size,
                                     size_t* out_data_actual, void* out_oob_buffer, size_t oob_size,
                                     size_t* out_oob_actual, uint32_t* out_ecc_correct) {
        if (nandpage > info_.pages_per_block * info_.num_blocks) {
            result_ = ZX_ERR_IO;
        }
        static_cast<uint8_t*>(out_data_buffer)[0] = 'd';
        static_cast<uint8_t*>(out_oob_buffer)[0] = 'o';
        *out_ecc_correct = ecc_bits_;

        last_op_.type = OperationType::kRead;
        last_op_.nandpage = nandpage;

        return result_;
    }

    zx_status_t RawNandWritePageHwecc(const void* data_buffer, size_t data_size,
                                      const void* oob_buffer, size_t oob_size, uint32_t nandpage) {
        if (nandpage > info_.pages_per_block * info_.num_blocks) {
            result_ = ZX_ERR_IO;
        }

        uint8_t byte = static_cast<const uint8_t*>(data_buffer)[0];
        if (byte != 'd') {
            result_ = ZX_ERR_IO;
        }

        byte = static_cast<const uint8_t*>(oob_buffer)[0];
        if (byte != 'o') {
            result_ = ZX_ERR_IO;
        }

        last_op_.type = OperationType::kWrite;
        last_op_.nandpage = nandpage;

        return result_;
    }

    zx_status_t RawNandEraseBlock(uint32_t nandpage) {
        last_op_.type = OperationType::kErase;
        last_op_.nandpage = nandpage;
        return result_;
    }

    const LastOperation& last_op() { return last_op_; }

private:
    raw_nand_protocol_t proto_;
    fuchsia_hardware_nand_Info info_ = kInfo;
    zx_status_t result_ = ZX_OK;
    uint32_t ecc_bits_ = 0;

    LastOperation last_op_ = {};
};

class NandTester {
public:
    NandTester() {
        fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
        protocols[0] = {ZX_PROTOCOL_RAW_NAND,
                        *reinterpret_cast<const fake_ddk::Protocol*>(raw_nand_.proto())};
        ddk_.SetProtocols(std::move(protocols));
        ddk_.SetSize(kPageSize * kNumPages * kNumBlocks);
    }

    fake_ddk::Bind& ddk() { return ddk_; }
    FakeRawNand& raw_nand() { return raw_nand_; }

private:
    fake_ddk::Bind ddk_;
    FakeRawNand raw_nand_;
};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    NandTester tester;
    nand::NandDevice device(fake_ddk::kFakeParent);
    ASSERT_EQ(ZX_OK, device.Init());
    END_TEST;
}

bool DdkLifetimeTest() {
    BEGIN_TEST;

    NandTester tester;
    nand::NandDevice* device(new nand::NandDevice(fake_ddk::kFakeParent));

    ASSERT_EQ(ZX_OK, device->Init());
    ASSERT_EQ(ZX_OK, device->Bind());
    device->DdkUnbind();
    EXPECT_TRUE(tester.ddk().Ok());

    // This should delete the object, which means this test should not leak.
    device->DdkRelease();
    END_TEST;
}

bool GetSizeTest() {
    BEGIN_TEST;
    NandTester tester;
    nand::NandDevice device(fake_ddk::kFakeParent);
    ASSERT_EQ(ZX_OK, device.Init());
    EXPECT_EQ(kPageSize * kNumPages * kNumBlocks, device.DdkGetSize());
    END_TEST;
}

bool QueryTest() {
    BEGIN_TEST;
    NandTester tester;
    nand::NandDevice device(fake_ddk::kFakeParent);
    ASSERT_EQ(ZX_OK, device.Init());

    fuchsia_hardware_nand_Info info;
    size_t operation_size;
    device.NandQuery(&info, &operation_size);

    ASSERT_EQ(0, memcmp(&info, &kInfo, sizeof(info)));
    ASSERT_GT(operation_size, sizeof(nand_operation_t));
    END_TEST;
}

class NandDeviceTest;

// Wrapper for a nand_operation_t.
class Operation {
public:
    explicit Operation(size_t op_size, NandDeviceTest* test)
        : op_size_(op_size), test_(test) {}
    ~Operation() {}

    // Accessors for the memory represented by the operation's vmo.
    size_t buffer_size() const { return buffer_size_; }
    void* buffer() const { return data_mapper_.start(); }

    size_t oob_buffer_size() const { return buffer_size_; }
    void* oob_buffer() const { return oob_mapper_.start(); }

    // Creates a vmo and sets the handle on the nand_operation_t.
    bool SetVmo();

    nand_operation_t* GetOperation();

    void OnCompletion(zx_status_t status) {
        status_ = status;
        completed_ = true;
    }

    bool completed() const { return completed_; }
    zx_status_t status() const { return status_; }
    NandDeviceTest* test() const { return test_; }

    DISALLOW_COPY_ASSIGN_AND_MOVE(Operation);

private:
    zx_handle_t GetDataVmo();
    zx_handle_t GetOobVmo();

    fzl::OwnedVmoMapper data_mapper_;
    fzl::OwnedVmoMapper oob_mapper_;
    size_t op_size_;
    NandDeviceTest* test_;
    zx_status_t status_ = ZX_ERR_ACCESS_DENIED;
    bool completed_ = false;
    static constexpr size_t buffer_size_ = kPageSize * kNumPages;
    static constexpr size_t oob_buffer_size_ = kOobSize * kNumPages;
    std::unique_ptr<char[]> raw_buffer_;
};

bool Operation::SetVmo() {
    nand_operation_t* operation = GetOperation();
    if (!operation) {
        return false;
    }
    operation->rw.data_vmo = GetDataVmo();
    operation->rw.oob_vmo = GetOobVmo();
    return operation->rw.data_vmo != ZX_HANDLE_INVALID &&
           operation->rw.oob_vmo != ZX_HANDLE_INVALID;
}

nand_operation_t* Operation::GetOperation() {
    if (!raw_buffer_) {
        raw_buffer_.reset(new char[op_size_]);
        memset(raw_buffer_.get(), 0, op_size_);
    }
    return reinterpret_cast<nand_operation_t*>(raw_buffer_.get());
}

zx_handle_t Operation::GetDataVmo() {
    if (data_mapper_.start()) {
        return data_mapper_.vmo().get();
    }

    if (data_mapper_.CreateAndMap(buffer_size_, "") != ZX_OK) {
        return ZX_HANDLE_INVALID;
    }

    return data_mapper_.vmo().get();
}

zx_handle_t Operation::GetOobVmo() {
    if (oob_mapper_.start()) {
        return oob_mapper_.vmo().get();
    }

    if (oob_mapper_.CreateAndMap(oob_buffer_size_, "") != ZX_OK) {
        return ZX_HANDLE_INVALID;
    }

    return oob_mapper_.vmo().get();
}

// Provides control primitives for tests that issue IO requests to the device.
class NandDeviceTest {
public:
    NandDeviceTest();
    ~NandDeviceTest() {}

    nand::NandDevice* device() { return device_.get(); }
    FakeRawNand& raw_nand() { return tester_.raw_nand(); }

    size_t op_size() const { return op_size_; }

    static void CompletionCb(void* cookie, zx_status_t status, nand_operation_t* op) {
        Operation* operation = reinterpret_cast<Operation*>(cookie);

        operation->OnCompletion(status);
        operation->test()->num_completed_++;
        sync_completion_signal(&operation->test()->event_);
    }

    bool Wait() {
        zx_status_t status = sync_completion_wait(&event_, ZX_SEC(5));
        sync_completion_reset(&event_);
        return status == ZX_OK;
    }

    bool WaitFor(int desired) {
        while (num_completed_ < desired) {
            if (!Wait()) {
                return false;
            }
        }
        return true;
    }

    DISALLOW_COPY_ASSIGN_AND_MOVE(NandDeviceTest);

private:
    sync_completion_t event_;
    std::atomic<int> num_completed_ = 0;
    NandTester tester_;
    std::unique_ptr<nand::NandDevice> device_;
    size_t op_size_;
};

NandDeviceTest::NandDeviceTest() {
    device_ = std::make_unique<nand::NandDevice>(fake_ddk::kFakeParent);

    fuchsia_hardware_nand_Info info;
    device_->NandQuery(&info, &op_size_);

    if (device_->Init() != ZX_OK) {
        device_.reset();
    }
}

// Tests trivial attempts to queue one operation.
bool QueueOneTest() {
    BEGIN_TEST;
    NandDeviceTest test;
    nand::NandDevice* device = test.device();
    ASSERT_TRUE(device);

    Operation operation(test.op_size(), &test);

    nand_operation_t* op = operation.GetOperation();
    ASSERT_TRUE(op);

    op->rw.command = NAND_OP_READ;
    device->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    op->rw.length = 1;
    device->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_BAD_HANDLE, operation.status());

    op->rw.offset_nand = kNumPages * kNumBlocks;
    device->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    ASSERT_TRUE(operation.SetVmo());

    op->rw.offset_nand = (kNumPages * kNumBlocks) - 1;
    device->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());
    END_TEST;
}

bool ReadWriteTest() {
    BEGIN_TEST;
    NandDeviceTest test;
    nand::NandDevice* device = test.device();
    FakeRawNand& raw_nand = test.raw_nand();
    ASSERT_TRUE(device);

    Operation operation(test.op_size(), &test);
    ASSERT_TRUE(operation.SetVmo());

    nand_operation_t* op = operation.GetOperation();
    ASSERT_TRUE(op);

    op->rw.command = NAND_OP_READ;
    op->rw.length = 2;
    op->rw.offset_nand = 3;
    ASSERT_TRUE(operation.SetVmo());
    device->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());

    EXPECT_EQ(raw_nand.last_op().type, OperationType::kRead);
    EXPECT_EQ(raw_nand.last_op().nandpage, 4);

    op->rw.command = NAND_OP_WRITE;
    op->rw.length = 4;
    op->rw.offset_nand = 5;
    memset(operation.buffer(), kMagic, kPageSize * 5);
    memset(operation.oob_buffer(), kOobMagic, kOobSize * 5);
    device->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());

    EXPECT_EQ(raw_nand.last_op().type, OperationType::kWrite);
    EXPECT_EQ(raw_nand.last_op().nandpage, 8);

    END_TEST;
}

bool EraseTest() {
    BEGIN_TEST;
    NandDeviceTest test;
    nand::NandDevice* device = test.device();
    ASSERT_TRUE(device);

    Operation operation(test.op_size(), &test);
    nand_operation_t* op = operation.GetOperation();
    ASSERT_TRUE(op);

    op->erase.command = NAND_OP_ERASE;
    op->erase.num_blocks = 1;
    op->erase.first_block = 5;
    device->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);

    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());

    EXPECT_EQ(test.raw_nand().last_op().type, OperationType::kErase);
    EXPECT_EQ(test.raw_nand().last_op().nandpage, 5 * kNumPages);

    END_TEST;
}

// Tests serialization of multiple operations.
bool QueueMultipleTest() {
    BEGIN_TEST;
    NandDeviceTest test;
    nand::NandDevice* device = test.device();
    ASSERT_TRUE(device);

    std::unique_ptr<Operation> operations[10];
    for (int i = 0; i < 10; i++) {
        operations[i].reset(new Operation(test.op_size(), &test));
        Operation& operation = *(operations[i].get());
        nand_operation_t* op = operation.GetOperation();
        ASSERT_TRUE(op);

        op->rw.command = NAND_OP_READ;
        op->rw.length = 1;
        op->rw.offset_nand = i;
        ASSERT_TRUE(operation.SetVmo());
        device->NandQueue(op, &NandDeviceTest::CompletionCb, &operation);
    }

    ASSERT_TRUE(test.WaitFor(10));

    for (const auto& operation : operations) {
        ASSERT_EQ(ZX_OK, operation->status());
        ASSERT_TRUE(operation->completed());
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(NandDeviceTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(DdkLifetimeTest)
RUN_TEST_SMALL(GetSizeTest)
RUN_TEST_SMALL(QueryTest)
RUN_TEST_SMALL(QueueOneTest)
RUN_TEST_SMALL(ReadWriteTest)
RUN_TEST_SMALL(EraseTest)
RUN_TEST_SMALL(QueueMultipleTest)
END_TEST_CASE(NandDeviceTests)
