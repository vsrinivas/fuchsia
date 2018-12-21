// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand_driver.h"

#include <memory>

#include <fbl/array.h>
#include <ddk/driver.h>
#include <ddktl/protocol/nand.h>
#include <ddktl/protocol/badblock.h>
#include <unittest/unittest.h>

namespace {

constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kOobSize = 8;
constexpr uint32_t kBlockSize = 4;
constexpr uint32_t kNumBlocks = 3;
constexpr uint32_t kEccBits = 12;

// Fake for the nand protocol.
class FakeNand : public ddk::NandProtocol<FakeNand> {
  public:
    FakeNand() : proto_({&nand_protocol_ops_, this}) {
        info_.page_size = kPageSize;
        info_.oob_size = kOobSize;
        info_.pages_per_block = kBlockSize;
        info_.num_blocks = kNumBlocks;
        info_.ecc_bits = kEccBits;
    }

    nand_protocol_t* proto() { return &proto_; }
    nand_operation_t* operation() { return operation_; }

    void set_result(zx_status_t result) { result_ = result; }
    void set_ecc_bits(uint32_t ecc_bits) { ecc_bits_ = ecc_bits; }

    // Nand protocol:
    void NandQuery(zircon_nand_Info* out_info, size_t* out_nand_op_size) {
        *out_info = info_;
        *out_nand_op_size = sizeof(*operation_);
    }

    void NandQueue(nand_operation_t* operation, nand_queue_callback callback, void* cookie) {
        operation_ = operation;
        if (operation->rw.command == NAND_OP_READ) {
            uint8_t data = 'd';
            uint64_t vmo_addr = operation->rw.offset_data_vmo * kPageSize;
            zx_vmo_write(operation->rw.data_vmo, &data, vmo_addr, sizeof(data));

            data = 'o';
            vmo_addr = operation->rw.offset_oob_vmo * kPageSize;
            zx_vmo_write(operation->rw.oob_vmo, &data, vmo_addr, sizeof(data));
            operation->rw.corrected_bit_flips = ecc_bits_;
        } else if (operation->rw.command == NAND_OP_WRITE) {
            uint8_t data;
            uint64_t vmo_addr = operation->rw.offset_data_vmo * kPageSize;
            zx_vmo_read(operation->rw.data_vmo, &data, vmo_addr, sizeof(data));
            if (data != 'd') {
                result_ = ZX_ERR_IO;
            }

            vmo_addr = operation->rw.offset_oob_vmo * kPageSize;
            zx_vmo_read(operation->rw.oob_vmo, &data, vmo_addr, sizeof(data));
            if (data != 'o') {
                result_ = ZX_ERR_IO;
            }
        }
        callback(cookie, result_, operation);
    }

    zx_status_t NandGetFactoryBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
                                           size_t* out_bad_blocks_actual) {
      return ZX_ERR_BAD_STATE;
    }

  private:
    nand_protocol_t proto_;
    zircon_nand_Info info_ = {};
    nand_operation_t* operation_;
    zx_status_t result_ = ZX_OK;
    uint32_t ecc_bits_ = 0;
};

// Fake for the bad block protocol.
class FakeBadBlock : public ddk::BadBlockProtocol<FakeBadBlock> {
  public:
    FakeBadBlock() : proto_({&bad_block_protocol_ops_, this}) {}

    bad_block_protocol_t* proto() { return &proto_; }
    void set_result(zx_status_t result) { result_ = result; }

    // Bad block protocol:
    zx_status_t BadBlockGetBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
                                        size_t* out_bad_blocks_actual) {
        *out_bad_blocks_actual = 0;
        if (!bad_blocks_count) {
            *out_bad_blocks_actual = 1;
        } else if (bad_blocks_count == 1) {
            ZX_ASSERT(out_bad_blocks_list);
            *out_bad_blocks_list = 1;  // Second block is bad.
            *out_bad_blocks_actual = 1;
        }
        return result_;
    }

    zx_status_t BadBlockMarkBlockBad(uint32_t block) {
        return ZX_ERR_BAD_STATE;
    }

  private:
    bad_block_protocol_t proto_;
    zx_status_t result_ = ZX_OK;
};

class NandTester {
  public:
    NandTester() {}

    nand_protocol_t* nand_proto() { return nand_proto_.proto(); }
    bad_block_protocol_t* bad_block_proto() { return bad_block_proto_.proto(); }
    nand_operation_t* operation() { return nand_proto_.operation(); }
    FakeNand* nand() { return &nand_proto_; }
    FakeBadBlock* bad_block() { return &bad_block_proto_; }

  private:
    FakeNand nand_proto_;
    FakeBadBlock bad_block_proto_;
};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    END_TEST;
}

bool InitTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());
    END_TEST;
}

bool InitFailureTest() {
    BEGIN_TEST;
    NandTester tester;

    tester.bad_block()->set_result(ZX_ERR_BAD_STATE);
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_NE(nullptr, driver->Init());
    END_TEST;
}

bool ReadTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize * 2], kPageSize * 2);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize * 2], kOobSize * 2);

    ASSERT_EQ(ftl::kNdmOk, driver->NandRead(5, 2, data.get(), oob.get()));

    nand_operation_t* operation = tester.operation();
    EXPECT_EQ(NAND_OP_READ, operation->command);
    EXPECT_EQ(2 * 2, operation->rw.length);
    EXPECT_EQ(5 * 2, operation->rw.offset_nand);
    EXPECT_EQ(0, operation->rw.offset_data_vmo);
    EXPECT_EQ(2 * 2, operation->rw.offset_oob_vmo);
    EXPECT_EQ('d', data[0]);
    EXPECT_EQ('o', oob[0]);
    END_TEST;
}

bool ReadFailureTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize * 2], kPageSize * 2);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize * 2], kOobSize * 2);

    tester.nand()->set_result(ZX_ERR_BAD_STATE);
    ASSERT_EQ(ftl::kNdmFatalError, driver->NandRead(5, 2, data.get(), oob.get()));
    END_TEST;
}

bool ReadEccUnsafeTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize * 2], kPageSize * 2);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize * 2], kOobSize * 2);

    tester.nand()->set_ecc_bits(kEccBits / 2 + 1);
    ASSERT_EQ(ftl::kNdmUnsafeEcc, driver->NandRead(5, 2, data.get(), oob.get()));
    END_TEST;
}

bool ReadEccFailureTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize * 2], kPageSize * 2);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize * 2], kOobSize * 2);

    tester.nand()->set_ecc_bits(kEccBits + 1);
    ASSERT_EQ(ftl::kNdmUncorrectableEcc, driver->NandRead(5, 2, data.get(), oob.get()));
    END_TEST;
}

bool WriteTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize * 2], kPageSize * 2);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize * 2], kOobSize * 2);
    memset(data.get(), 'd', data.size());
    memset(oob.get(), 'o', oob.size());

    ASSERT_EQ(ftl::kNdmOk, driver->NandWrite(5, 2, data.get(), oob.get()));

    nand_operation_t* operation = tester.operation();
    EXPECT_EQ(NAND_OP_WRITE, operation->command);
    EXPECT_EQ(2 * 2, operation->rw.length);
    EXPECT_EQ(5 * 2, operation->rw.offset_nand);
    EXPECT_EQ(0, operation->rw.offset_data_vmo);
    EXPECT_EQ(2 * 2, operation->rw.offset_oob_vmo);
    END_TEST;
}

bool WriteFailureTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    fbl::Array<uint8_t> data(new uint8_t[kPageSize * 2], kPageSize * 2);
    fbl::Array<uint8_t> oob(new uint8_t[kOobSize * 2], kOobSize * 2);
    memset(data.get(), 'd', data.size());
    memset(oob.get(), 'e', oob.size());  // Unexpected value.

    ASSERT_EQ(ftl::kNdmError, driver->NandWrite(5, 2, data.get(), oob.get()));
    END_TEST;
}

bool EraseTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    ASSERT_EQ(ftl::kNdmOk, driver->NandErase(5 * kBlockSize / 2));

    nand_operation_t* operation = tester.operation();
    EXPECT_EQ(NAND_OP_ERASE, operation->command);
    EXPECT_EQ(1, operation->erase.num_blocks);
    EXPECT_EQ(5, operation->erase.first_block);
    END_TEST;
}

bool EraseFailureTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    tester.nand()->set_result(ZX_ERR_BAD_STATE);
    ASSERT_EQ(ftl::kNdmError, driver->NandErase(5 * kBlockSize / 2));
    END_TEST;
}

bool IsBadBlockTest() {
    BEGIN_TEST;
    NandTester tester;
    auto driver = ftl::NandDriver::Create(tester.nand_proto(), tester.bad_block_proto());
    ASSERT_EQ(nullptr, driver->Init());

    ASSERT_FALSE(driver->IsBadBlock(0));
    ASSERT_TRUE(driver->IsBadBlock(1 * kBlockSize / 2));
    ASSERT_FALSE(driver->IsBadBlock(2 * kBlockSize / 2));
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(NandDriverTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(InitTest)
RUN_TEST_SMALL(InitFailureTest)
RUN_TEST_SMALL(ReadTest)
RUN_TEST_SMALL(ReadFailureTest)
RUN_TEST_SMALL(ReadEccUnsafeTest)
RUN_TEST_SMALL(ReadEccFailureTest)
RUN_TEST_SMALL(WriteTest)
RUN_TEST_SMALL(WriteFailureTest)
RUN_TEST_SMALL(EraseTest)
RUN_TEST_SMALL(EraseFailureTest)
RUN_TEST_SMALL(IsBadBlockTest)
END_TEST_CASE(NandDriverTests)
