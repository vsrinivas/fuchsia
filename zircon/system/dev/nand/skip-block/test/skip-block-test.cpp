// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skip-block.h"

#include <ddktl/protocol/badblock.h>
#include <ddktl/protocol/nand.h>
#include <fbl/vector.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kOobSize = 8;
constexpr uint32_t kNumPages = 20;
constexpr uint32_t kBlockSize = kPageSize * kNumPages;
constexpr uint32_t kNumBlocks = 10;
constexpr uint32_t kEccBits = 10;

fuchsia_hardware_nand_Info kInfo = {kPageSize, kNumPages, kNumBlocks, kEccBits, kOobSize, 0, {}};

// We inject a special context as parent so that we can store the device.
struct Context {
    nand::SkipBlockDevice* dev = nullptr;
};

class Binder : public fake_ddk::Bind {
public:
    zx_status_t DeviceRemove(zx_device_t* dev) override {
        Context* context = reinterpret_cast<Context*>(dev);
        if (context->dev) {
            context->dev->DdkRelease();
        }
        context->dev = nullptr;
        return ZX_OK;
    }
    zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                          zx_device_t** out) override {
        *out = parent;
        Context* context = reinterpret_cast<Context*>(parent);
        context->dev = reinterpret_cast<nand::SkipBlockDevice*>(args->ctx);
        return ZX_OK;
    }
    zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id, void* protocol) override {
        auto out = reinterpret_cast<fake_ddk::Protocol*>(protocol);
        for (const auto& proto : protocols_) {
            if (proto_id == proto.id) {
                out->ops = proto.proto.ops;
                out->ctx = proto.proto.ctx;
                return ZX_OK;
            }
        }
        return ZX_ERR_NOT_SUPPORTED;
    }
};

// Fake for the nand protocol.
class FakeNand : public ddk::NandProtocol<FakeNand> {
public:
    FakeNand()
        : proto_({&nand_protocol_ops_, this}) {}

    const nand_protocol_t* proto() const { return &proto_; }

    void set_result(zx_status_t result) { result_.push_back(result); }

    // Nand protocol:
    void NandQuery(fuchsia_hardware_nand_Info* info_out, size_t* nand_op_size_out) {
        *info_out = nand_info_;
        *nand_op_size_out = sizeof(nand_operation_t);
    }

    void NandQueue(nand_operation_t* op, nand_queue_callback completion_cb, void* cookie) {
        last_op_ = op->command;

        auto result = result_[call_++];

        if (result != ZX_OK) {
            completion_cb(cookie, result, op);
            return;
        }

        switch (op->command) {
        case NAND_OP_READ: {
            if (op->rw.offset_nand >= num_nand_pages_ || !op->rw.length ||
                (num_nand_pages_ - op->rw.offset_nand) < op->rw.length) {
                result = ZX_ERR_OUT_OF_RANGE;
                break;
            }
            if (op->rw.data_vmo == ZX_HANDLE_INVALID &&
                op->rw.oob_vmo == ZX_HANDLE_INVALID) {
                result = ZX_ERR_BAD_HANDLE;
                break;
            }
            break;
        }
        case NAND_OP_WRITE: {
            if (op->rw.offset_nand >= num_nand_pages_ || !op->rw.length ||
                (num_nand_pages_ - op->rw.offset_nand) < op->rw.length) {
                result = ZX_ERR_OUT_OF_RANGE;
                break;
            }
            if (op->rw.data_vmo == ZX_HANDLE_INVALID &&
                op->rw.oob_vmo == ZX_HANDLE_INVALID) {
                result = ZX_ERR_BAD_HANDLE;
                break;
            }
            break;
        }
        case NAND_OP_ERASE:
            if (!op->erase.num_blocks ||
                op->erase.first_block >= nand_info_.num_blocks ||
                (op->erase.num_blocks > (nand_info_.num_blocks - op->erase.first_block))) {
                result = ZX_ERR_OUT_OF_RANGE;
            }
            break;

        default:
            result = ZX_ERR_NOT_SUPPORTED;
            break;
        }

        completion_cb(cookie, result, op);
        return;
    }

    zx_status_t NandGetFactoryBadBlockList(uint32_t* bad_blocks, size_t bad_block_len,
                                           size_t* num_bad_blocks) {
        *num_bad_blocks = 0;
        return ZX_OK;
    }

    const nand_op_t& last_op() { return last_op_; }

private:
    size_t call_ = 0;
    nand_protocol_t proto_;
    fuchsia_hardware_nand_Info nand_info_ = kInfo;
    fbl::Vector<zx_status_t> result_;
    size_t num_nand_pages_ = kNumPages * kNumBlocks;

    nand_op_t last_op_ = {};
};

// Fake for the bad block protocol.
class FakeBadBlock : public ddk::BadBlockProtocol<FakeBadBlock> {
public:
    FakeBadBlock()
        : proto_({&bad_block_protocol_ops_, this}) {}

    const bad_block_protocol_t* proto() const { return &proto_; }

    void set_result(zx_status_t result) { result_ = result; }

    // Bad Block protocol:
    zx_status_t BadBlockGetBadBlockList(uint32_t* bad_block_list, size_t bad_block_list_len,
                                        size_t* bad_block_count) {
        *bad_block_count = grown_bad_blocks_.size();
        if (bad_block_list_len < *bad_block_count) {
            return bad_block_list == nullptr ? ZX_OK : ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(bad_block_list, grown_bad_blocks_.get(), grown_bad_blocks_.size());
        return result_;
    }
    zx_status_t BadBlockMarkBlockBad(uint32_t block) {
        if (result_ == ZX_OK) {
            grown_bad_blocks_.push_back(block);
        }
        return result_;
    }

    const fbl::Vector<uint32_t>& grown_bad_blocks() { return grown_bad_blocks_; }

private:
    fbl::Vector<uint32_t> grown_bad_blocks_;
    bad_block_protocol_t proto_;
    zx_status_t result_ = ZX_OK;
};

class SkipBlockTest : public zxtest::Test {
protected:
    SkipBlockTest() {
        fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[2], 2);
        protocols[0] = {ZX_PROTOCOL_NAND,
                        *reinterpret_cast<const fake_ddk::Protocol*>(nand_.proto())};
        protocols[1] = {ZX_PROTOCOL_BAD_BLOCK,
                        *reinterpret_cast<const fake_ddk::Protocol*>(bad_block_.proto())};
        ddk_.SetProtocols(std::move(protocols));
        ddk_.SetSize(kPageSize * kNumPages * kNumBlocks);
        ddk_.SetMetadata(&count_, sizeof(count_));
    }

    ~SkipBlockTest() {
        ddk_.DeviceRemove(parent());
    }

    zx_device_t* parent() { return reinterpret_cast<zx_device_t*>(&ctx_); }
    nand::SkipBlockDevice& dev() { return *ctx_.dev; }
    Binder& ddk() { return ddk_; }
    FakeNand& nand() { return nand_; }
    FakeBadBlock& bad_block() { return bad_block_; }

private:
    const uint32_t count_ = 1;
    Context ctx_ = {};
    Binder ddk_;
    FakeNand nand_;
    FakeBadBlock bad_block_;
};

TEST_F(SkipBlockTest, Create) {
    ASSERT_OK(nand::SkipBlockDevice::Create(parent()));
}

TEST_F(SkipBlockTest, GrowBadBlock) {
    ASSERT_OK(nand::SkipBlockDevice::Create(parent()));

    nand().set_result(ZX_OK);
    nand().set_result(ZX_ERR_IO);
    nand().set_result(ZX_OK);
    nand().set_result(ZX_OK);

    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(fbl::round_up(kBlockSize, ZX_PAGE_SIZE), 0, &vmo));
    nand::ReadWriteOperation op = {};
    op.vmo = vmo.release();
    op.block = 5;
    op.block_count = 1;

    bool bad_block_grown;
    ASSERT_OK(dev().Write(op, &bad_block_grown));
    ASSERT_TRUE(bad_block_grown);
    ASSERT_EQ(bad_block().grown_bad_blocks().size(), 1);
    ASSERT_EQ(bad_block().grown_bad_blocks()[0], 5);
    ASSERT_EQ(nand().last_op(), NAND_OP_WRITE);
}

TEST_F(SkipBlockTest, GrowMultipleBadBlock) {
    ASSERT_OK(nand::SkipBlockDevice::Create(parent()));

    // Erase Block 5
    nand().set_result(ZX_OK);
    // Write Block 5
    nand().set_result(ZX_ERR_IO);
    // Erase Block 6
    nand().set_result(ZX_ERR_IO);
    // Erase Block 7
    nand().set_result(ZX_OK);
    // Write Block 7
    nand().set_result(ZX_OK);

    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(fbl::round_up(kBlockSize, ZX_PAGE_SIZE), 0, &vmo));
    nand::ReadWriteOperation op = {};
    op.vmo = vmo.release();
    op.block = 5;
    op.block_count = 1;

    bool bad_block_grown;
    ASSERT_OK(dev().Write(op, &bad_block_grown));
    ASSERT_TRUE(bad_block_grown);
    ASSERT_EQ(bad_block().grown_bad_blocks().size(), 2);
    ASSERT_EQ(bad_block().grown_bad_blocks()[0], 5);
    ASSERT_EQ(bad_block().grown_bad_blocks()[1], 6);
    ASSERT_EQ(nand().last_op(), NAND_OP_WRITE);
}

TEST_F(SkipBlockTest, MappingFailure) {
    ASSERT_OK(nand::SkipBlockDevice::Create(parent()));

    // Erase Block 5
    nand().set_result(ZX_OK);
    // Write Block 5
    nand().set_result(ZX_ERR_INVALID_ARGS);

    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(fbl::round_up(kBlockSize, ZX_PAGE_SIZE), 0, &vmo));
    nand::ReadWriteOperation op = {};
    op.vmo = vmo.release();
    op.block = 5;
    op.block_count = 1;

    bool bad_block_grown;
    ASSERT_EQ(dev().Write(op, &bad_block_grown), ZX_ERR_INVALID_ARGS);
    ASSERT_FALSE(bad_block_grown);
    ASSERT_EQ(bad_block().grown_bad_blocks().size(), 0);
    ASSERT_EQ(nand().last_op(), NAND_OP_WRITE);
}
