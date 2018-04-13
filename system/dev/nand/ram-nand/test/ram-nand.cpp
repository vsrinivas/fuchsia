// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>
#include <zircon/process.h>

#include "ram-nand.h"

namespace {

const int kPageSize = 4096;
const int kBlockSize = 4;
const int kNumBlocks = 5;
const int kNumPages = kBlockSize * kNumBlocks;

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 0);  // 6 bits of ECC, no OOB.
    {
        NandDevice device(params);

        device_add_args_t args;
        ASSERT_EQ(ZX_OK, device.Init(nullptr, &args));
        EXPECT_EQ(&device, args.ctx);
        EXPECT_EQ(0, strncmp("ram-nand-0", args.name, 32));
        EXPECT_EQ(ZX_PROTOCOL_NAND, args.proto_id);
    }
    {
        NandDevice device(params);

        device_add_args_t args;
        ASSERT_EQ(ZX_OK, device.Init(nullptr, &args));
        EXPECT_EQ(0, strncmp("ram-nand-1", args.name, 32));
    }
    END_TEST;
}

fbl::unique_ptr<NandDevice> CreateDevice(size_t* operation_size) {
    NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 4);  // 6 bits of ECC, 4 OOB bytes.
    fbl::AllocChecker checker;
    fbl::unique_ptr<NandDevice> device(new (&checker) NandDevice(params));
    if (!checker.check()) {
        return nullptr;
    }

    if (operation_size) {
        nand_info_t info;
        device->Query(&info, operation_size);
    }

    device_add_args_t args;
    if (device->Init(nullptr, &args) != ZX_OK) {
        return nullptr;
    }
    return fbl::move(device);
}

class UnbindChecker;
UnbindChecker* g_unbind = nullptr;  // Only valid during the test.

class UnbindChecker {
  public:
    explicit UnbindChecker(zx_device_t* device) : device_(device) {}
    ~UnbindChecker() {}

    bool AllOk() const { return ok_; }

    static void UnbindCallback(zx_device_t* device) {
        if (g_unbind->called_) {
            g_unbind->ok_ = false;
        } else {
            g_unbind->ok_ = (device == g_unbind->device_);
        }
        g_unbind->called_ = true;
    }

  private:
    zx_device_t* device_;
    bool ok_ = false;
    bool called_ = false;
    DISALLOW_COPY_ASSIGN_AND_MOVE(UnbindChecker);
};

bool BasicDeviceProtocolTest() {
    BEGIN_TEST;
    NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 0);  // 6 bits of ECC, no OOB.
    NandDevice device(params);

    zx_device_t* zx_dev = reinterpret_cast<zx_device_t*>(42);
    UnbindChecker checker(zx_dev);
    g_unbind = &checker;

    device_add_args_t args;
    ASSERT_EQ(ZX_OK, device.Init(&checker.UnbindCallback, &args));
    device.SetDevice(zx_dev);

    ASSERT_EQ(kPageSize * kNumPages, device.GetSize());

    device.Unbind();
    ASSERT_TRUE(checker.AllOk());

    device.Unbind();
    ASSERT_FALSE(checker.AllOk());
    END_TEST;
}

bool UnlinkTest() {
    BEGIN_TEST;
    fbl::unique_ptr<NandDevice> device = CreateDevice(nullptr);
    ASSERT_TRUE(device);

    ASSERT_EQ(ZX_OK, device->IoCtl(IOCTL_RAM_NAND_UNLINK, nullptr, 0, nullptr, 0, nullptr));

    // The device is "dead" now.
    ASSERT_EQ(ZX_ERR_BAD_STATE,
              device->IoCtl(IOCTL_RAM_NAND_UNLINK, nullptr, 0, nullptr, 0, nullptr));
    END_TEST;
}

bool QueryTest() {
    BEGIN_TEST;
    NandParams params(kPageSize, kBlockSize, kNumBlocks, 6, 8);  // 6 bits of ECC, 8 OOB bytes.
    NandDevice device(params);

    nand_info_t info;
    size_t operation_size;
    device.Query(&info, &operation_size);
    ASSERT_EQ(0, memcmp(&info, &params, sizeof(info)));
    ASSERT_GT(operation_size, sizeof(nand_op_t));
    END_TEST;
}

// Tests setting and getting bad blocks.
bool BadBlockListTest() {
    BEGIN_TEST;
    fbl::unique_ptr<NandDevice> device = CreateDevice(nullptr);
    ASSERT_TRUE(device);

    uint32_t bad_blocks[] = {1, 3, 5};
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
              device->IoCtl(IOCTL_RAM_NAND_SET_BAD_BLOCKS, bad_blocks, sizeof(bad_blocks),
                            nullptr, 0, nullptr));

    uint32_t result[4];
    uint32_t num_bad_blocks;
    device->GetBadBlockList(result, sizeof(result), &num_bad_blocks);
    ASSERT_EQ(0, num_bad_blocks);
    END_TEST;
}

// Data to be pre-pended to a nand_op_t issued to the device.
struct OpHeader {
    class Operation* operation;
    class NandTest* test;
};

// Wrapper for a nand_op_t.
class Operation {
  public:
    explicit Operation(size_t op_size, NandTest* test = 0)
        : op_size_(op_size + sizeof(OpHeader)), test_(test) {}
    ~Operation() {
        if (mapped_addr_) {
            zx_vmar_unmap(zx_vmar_root_self(), reinterpret_cast<uintptr_t>(mapped_addr_),
                          buffer_size_);
        }
    }

    // Accessors for the memory represented by the operation's vmo.
    size_t buffer_size() const { return buffer_size_; }
    char* buffer() const { return mapped_addr_; }

    // Creates a vmo and sets the handle on the nand_op_t.
    bool SetVmo();

    nand_op_t* GetOperation();

    void OnCompletion(zx_status_t status) {
        status_ = status;
        completed_ = true;
    }

    bool completed() const { return completed_; }
    zx_status_t status() const { return status_; }

  private:
    zx_handle_t GetVmo();
    void CreateOperation();

    zx::vmo vmo_;
    char* mapped_addr_ = nullptr;
    size_t op_size_;
    NandTest* test_;
    zx_status_t status_ = ZX_ERR_ACCESS_DENIED;
    bool completed_ = false;
    static constexpr size_t buffer_size_ = kPageSize * kBlockSize;
    fbl::unique_ptr<char[]> raw_buffer_;
    DISALLOW_COPY_ASSIGN_AND_MOVE(Operation);
};

bool Operation::SetVmo() {
    nand_op_t* operation = GetOperation();
    if (!operation) {
        return false;
    }
    operation->rw.vmo = GetVmo();
    return operation->rw.vmo != ZX_HANDLE_INVALID;
}

nand_op_t* Operation::GetOperation() {
    if (!raw_buffer_) {
        CreateOperation();
    }
    return reinterpret_cast<nand_op_t*>(raw_buffer_.get() + sizeof(OpHeader));
}

zx_handle_t Operation::GetVmo() {
    if (vmo_.is_valid()) {
        return vmo_.get();
    }

    zx_status_t status = zx::vmo::create(buffer_size_, 0, &vmo_);
    if (status != ZX_OK) {
        return ZX_HANDLE_INVALID;
    }

    uintptr_t address;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_.get(), 0, buffer_size_,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                         &address);
    if (status != ZX_OK) {
        return ZX_HANDLE_INVALID;
    }
    mapped_addr_ = reinterpret_cast<char*>(address);
    return vmo_.get();
}

void Operation::CreateOperation() {
    fbl::AllocChecker checker;
    raw_buffer_.reset(new (&checker) char[op_size_]);
    if (!checker.check()) {
        return;
    }

    memset(raw_buffer_.get(), 0, op_size_);
    OpHeader* header = reinterpret_cast<OpHeader*>(raw_buffer_.get());
    header->operation = this;
    header->test = test_;
}

// Provides control primitives for tests that issue IO requests to the device.
class NandTest {
  public:
    NandTest() {}
    ~NandTest() {}

    static void CompletionCb(nand_op_t* op, zx_status_t status) {
        OpHeader* header =
                reinterpret_cast<OpHeader*>(reinterpret_cast<char*>(op) - sizeof(OpHeader));

        header->operation->OnCompletion(status);
        header->test->num_completed_++;
        completion_signal(&header->test->event_);
    }

    bool Wait() {
        zx_status_t status = completion_wait(&event_, ZX_SEC(5));
        completion_reset(&event_);
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

  private:
    completion_t event_;
    int num_completed_ = 0;
    DISALLOW_COPY_ASSIGN_AND_MOVE(NandTest);
};

// Tests trivial attempts to queue one operation.
bool QueueOneTest() {
    BEGIN_TEST;

    size_t op_size;
    fbl::unique_ptr<NandDevice> device = CreateDevice(&op_size);
    ASSERT_TRUE(device);

    NandTest test;
    Operation operation(op_size, &test);

    nand_op_t* op = operation.GetOperation();
    ASSERT_TRUE(op);

    op->rw.command = NAND_OP_WRITE_DATA;
    op->completion_cb = &NandTest::CompletionCb;
    device->Queue(op);

    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    op->rw.length = 1;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_BAD_HANDLE, operation.status());

    op->rw.offset_nand = kNumPages;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    ASSERT_TRUE(operation.SetVmo());

    op->rw.offset_nand = kNumPages - 1;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());

    END_TEST;
}

// Verifies that the buffer pointed to by the operation's vmo contains the given
// pattern for the desired number of pages, skipping the pages before start.
bool CheckPattern(char what, int start, int num_pages, const Operation& operation) {
    const char* buffer = operation.buffer() + kPageSize * start;
    for (int i = 0; i < kPageSize * num_pages; i++) {
        if (buffer[i] != what) {
            return false;
        }
    }
    return true;
}

// Prepares the operation to write num_pages starting at offset.
void SetForWrite(int offset, int num_pages, Operation* operation) {
    nand_op_t* op = operation->GetOperation();
    op->rw.command = NAND_OP_WRITE_DATA;
    op->rw.length = num_pages;
    op->rw.offset_nand = offset;
    op->completion_cb = &NandTest::CompletionCb;
}

// Prepares the operation to read num_pages starting at offset.
void SetForRead(int offset, int num_pages, Operation* operation) {
    nand_op_t* op = operation->GetOperation();
    op->rw.command = NAND_OP_READ_DATA;
    op->rw.length = num_pages;
    op->rw.offset_nand = offset;
    op->completion_cb = &NandTest::CompletionCb;
}

bool ReadWriteTest() {
    BEGIN_TEST;

    size_t op_size;
    fbl::unique_ptr<NandDevice> device = CreateDevice(&op_size);
    ASSERT_TRUE(device);

    NandTest test;
    Operation operation(op_size, &test);
    ASSERT_TRUE(operation.SetVmo());
    memset(operation.buffer(), 0x55, operation.buffer_size());

    nand_op_t* op = operation.GetOperation();
    op->rw.corrected_bit_flips = 125;

    SetForWrite(4, 4, &operation);
    device->Queue(op);

    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());
    ASSERT_EQ(125, op->rw.corrected_bit_flips);  // Doesn't modify the value.

    op->rw.command = NAND_OP_READ_DATA;
    memset(operation.buffer(), 0, operation.buffer_size());

    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());
    ASSERT_EQ(0, op->rw.corrected_bit_flips);
    ASSERT_TRUE(CheckPattern(0x55, 0, 4, operation));

    END_TEST;
}

// Tests serialization of multiple reads and writes.
bool QueueMultipleTest() {
    BEGIN_TEST;

    size_t op_size;
    fbl::unique_ptr<NandDevice> device = CreateDevice(&op_size);
    ASSERT_TRUE(device);

    NandTest test;
    fbl::unique_ptr<Operation> operations[10];
    for (int i = 0; i < 10; i++) {
        fbl::AllocChecker checker;
        operations[i].reset(new (&checker) Operation(op_size, &test));
        ASSERT_TRUE(checker.check());
        Operation& operation = *(operations[i].get());
        ASSERT_TRUE(operation.SetVmo());
        memset(operation.buffer(), i + 30, operation.buffer_size());
    }

    SetForWrite(0, 1, operations[0].get());  // 0 x x x x x
    SetForWrite(1, 3, operations[1].get());  // 0 1 1 1 x x
    SetForRead(0, 4, operations[2].get());
    SetForWrite(4, 2, operations[3].get());  // 0 1 1 1 3 3
    SetForRead(2, 4, operations[4].get());
    SetForWrite(2, 2, operations[5].get());  // 0 1 5 5 3 3
    SetForRead(0, 4, operations[6].get());
    SetForWrite(0, 4, operations[7].get());  // 7 7 7 7 3 3
    SetForRead(2, 4, operations[8].get());
    SetForRead(0, 2, operations[9].get());

    for (const auto& operation : operations) {
        nand_op_t* op = operation->GetOperation();
        device->Queue(op);
    }

    ASSERT_TRUE(test.WaitFor(10));

    for (const auto& operation : operations) {
        ASSERT_EQ(ZX_OK, operation->status());
        ASSERT_TRUE(operation->completed());
    }

    ASSERT_TRUE(CheckPattern(30, 0, 1, *(operations[2].get())));
    ASSERT_TRUE(CheckPattern(31, 1, 3, *(operations[2].get())));

    ASSERT_TRUE(CheckPattern(31, 0, 2, *(operations[4].get())));
    ASSERT_TRUE(CheckPattern(33, 2, 2, *(operations[4].get())));

    ASSERT_TRUE(CheckPattern(30, 0, 1, *(operations[6].get())));
    ASSERT_TRUE(CheckPattern(31, 1, 1, *(operations[6].get())));
    ASSERT_TRUE(CheckPattern(35, 2, 2, *(operations[6].get())));

    ASSERT_TRUE(CheckPattern(37, 0, 2, *(operations[8].get())));
    ASSERT_TRUE(CheckPattern(33, 2, 2, *(operations[8].get())));

    ASSERT_TRUE(CheckPattern(37, 0, 2, *(operations[9].get())));

    END_TEST;
}

bool OobLimitsTest() {
    BEGIN_TEST;

    size_t op_size;
    fbl::unique_ptr<NandDevice> device = CreateDevice(&op_size);
    ASSERT_TRUE(device);

    NandTest test;
    Operation operation(op_size, &test);

    nand_op_t* op = operation.GetOperation();
    op->oob.command = NAND_OP_READ_OOB;
    op->completion_cb = &NandTest::CompletionCb;

    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    op->oob.length = 1;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_BAD_HANDLE, operation.status());

    op->oob.page_num = kNumPages;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    ASSERT_TRUE(operation.SetVmo());

    op->oob.page_num = kNumPages - 1;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());

    op->oob.length = 5;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    END_TEST;
}

bool ReadWriteOobTest() {
    BEGIN_TEST;

    size_t op_size;
    fbl::unique_ptr<NandDevice> device = CreateDevice(&op_size);
    ASSERT_TRUE(device);

    NandTest test;
    Operation operation(op_size, &test);
    ASSERT_TRUE(operation.SetVmo());

    const char desired[4] = { 'a', 'b', 'c', 'd' };
    memcpy(operation.buffer(), desired, sizeof(desired));

    nand_op_t* op = operation.GetOperation();
    op->oob.corrected_bit_flips = 125;

    op->oob.command = NAND_OP_WRITE_OOB;
    op->oob.length = sizeof(desired);
    op->oob.page_num = 2;
    op->completion_cb = &NandTest::CompletionCb;
    device->Queue(op);

    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());
    ASSERT_EQ(125, op->oob.corrected_bit_flips);  // Doesn't modify the value.

    op->oob.command = NAND_OP_READ_OOB;
    memset(operation.buffer(), 0, sizeof(desired));

    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());
    ASSERT_EQ(0, op->oob.corrected_bit_flips);

    ASSERT_EQ(0, memcmp(operation.buffer(), desired, sizeof(desired)));

    END_TEST;
}

bool EraseLimitsTest() {
    BEGIN_TEST;

    size_t op_size;
    fbl::unique_ptr<NandDevice> device = CreateDevice(&op_size);
    ASSERT_TRUE(device);

    NandTest test;
    Operation operation(op_size, &test);
    ASSERT_TRUE(operation.SetVmo());

    nand_op_t* op = operation.GetOperation();
    op->erase.command = NAND_OP_ERASE;
    op->completion_cb = &NandTest::CompletionCb;

    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    op->erase.first_block = 5;
    op->erase.num_blocks = 1;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    op->erase.first_block = 4;
    op->erase.num_blocks = 2;
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, operation.status());

    END_TEST;
}

bool EraseTest() {
    BEGIN_TEST;

    size_t op_size;
    fbl::unique_ptr<NandDevice> device = CreateDevice(&op_size);
    ASSERT_TRUE(device);

    NandTest test;
    Operation operation(op_size, &test);
    ASSERT_TRUE(operation.SetVmo());

    nand_op_t* op = operation.GetOperation();
    op->erase.command = NAND_OP_ERASE;
    op->erase.first_block = 3;
    op->erase.num_blocks = 2;
    op->completion_cb = &NandTest::CompletionCb;

    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());

    SetForRead(12, 4, &operation);
    ASSERT_TRUE(operation.SetVmo());
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());
    ASSERT_TRUE(CheckPattern(static_cast<char>(0xff), 0, 4, operation));

    SetForRead(16, 4, &operation);
    device->Queue(op);
    ASSERT_TRUE(test.Wait());
    ASSERT_EQ(ZX_OK, operation.status());
    ASSERT_TRUE(CheckPattern(static_cast<char>(0xff), 0, 4, operation));

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(RamNandTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(BasicDeviceProtocolTest)
RUN_TEST_SMALL(UnlinkTest)
RUN_TEST_SMALL(QueryTest)
RUN_TEST_SMALL(BadBlockListTest)
RUN_TEST_SMALL(QueueOneTest)
RUN_TEST_SMALL(ReadWriteTest)
RUN_TEST_SMALL(QueueMultipleTest)
RUN_TEST_SMALL(OobLimitsTest)
RUN_TEST_SMALL(ReadWriteOobTest)
RUN_TEST_SMALL(EraseLimitsTest)
RUN_TEST_SMALL(EraseTest)
END_TEST_CASE(RamNandTests)
