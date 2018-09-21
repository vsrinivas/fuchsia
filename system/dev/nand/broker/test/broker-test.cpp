// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <fbl/unique_fd.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/mapped-vmo.h>
#include <unittest/unittest.h>
#include <zircon/device/device.h>
#include <zircon/device/nand-broker.h>
#include <zircon/syscalls.h>
#include <zxcpp/new.h>

#include "parent.h"

namespace {

constexpr uint32_t kMinOobSize = 4;
constexpr uint32_t kMinBlockSize = 4;
constexpr uint32_t kMinNumBlocks = 5;
constexpr uint32_t kInMemoryPages = 20;

fbl::unique_fd OpenBroker(const char* path) {
    fbl::unique_fd broker;

    auto callback = [](int dir_fd, int event, const char* filename, void* cookie) {
        if (event != WATCH_EVENT_ADD_FILE || strcmp(filename, "broker") != 0) {
            return ZX_OK;
        }
        fbl::unique_fd* broker = reinterpret_cast<fbl::unique_fd*>(cookie);
        broker->reset(openat(dir_fd, filename, O_RDWR));
        return ZX_ERR_STOP;
    };

    fbl::unique_fd dir(open(path, O_DIRECTORY));
    if (dir) {
        zx_time_t deadline = zx_deadline_after(ZX_SEC(5));
        fdio_watch_directory(dir.get(), callback, deadline, &broker);
    }
    return broker;
}

// The device under test.
class NandDevice {
public:
    NandDevice();
    ~NandDevice() {
        if (linked_) {
            ioctl_nand_broker_unlink(broker_.get());
        }
    }

    bool IsValid() const { return is_valid_; }

    // Returns the device's file descriptor.
    int get() const { return parent_->IsBroker() ? parent_->get() : broker_.get(); }

    // Wrappers for "queue" operations that take care of preserving the vmo's handle
    // and translating the request to the desired block range on the actual device.
    bool Read(const fzl::MappedVmo& vmo, const nand_broker_request_t& request,
              nand_broker_response_t* response);
    bool Write(const fzl::MappedVmo& vmo, const nand_broker_request_t& request,
               nand_broker_response_t* response);
    bool Erase(const nand_broker_request_t& request, nand_broker_response_t* response);

    // Erases a given block number.
    bool EraseBlock(uint32_t block_num);

    // Verifies that the buffer pointed to by the operation's vmo contains the given
    // pattern for the desired number of pages, skipping the pages before start.
    bool CheckPattern(uint8_t expected, int start, int num_pages, const void* memory);

    const nand_info_t& Info() const { return parent_->Info(); }

    uint32_t PageSize() const { return parent_->Info().page_size; }
    uint32_t OobSize() const { return parent_->Info().oob_size; }
    uint32_t BlockSize() const { return parent_->Info().pages_per_block; }
    uint32_t NumBlocks() const { return num_blocks_; }
    uint32_t NumPages() const { return num_blocks_ * BlockSize(); }
    uint32_t MaxBufferSize() const { return kInMemoryPages * (PageSize() + OobSize()); }

    // True when the whole device under test can be modified.
    bool IsFullDevice() const { return full_device_; }

private:
    bool ValidateNandDevice();

    ParentDevice* parent_ = g_parent_device_;
    fbl::unique_fd broker_;
    uint32_t num_blocks_ = 0;
    uint32_t first_block_ = 0;
    bool full_device_ = true;
    bool linked_ = false;
    bool is_valid_ = false;
};

NandDevice::NandDevice() {
    ZX_ASSERT(parent_->IsValid());
    if (!parent_->IsBroker()) {
        const char kBroker[] = "/boot/driver/nand-broker.so";
        if (ioctl_device_bind(parent_->get(), kBroker, sizeof(kBroker) - 1) < 0) {
            unittest_printf_critical("Failed to bind broker\n");
            return;
        }
        linked_ = true;
        broker_ = OpenBroker(parent_->Path());
    }
    is_valid_ = ValidateNandDevice();
}

bool NandDevice::Read(const fzl::MappedVmo& vmo, const nand_broker_request_t& request,
                      nand_broker_response_t* response) {
    BEGIN_TEST;
    nand_broker_request_t request_copy = request;
    if (!full_device_) {
        request_copy.offset_nand = request.offset_nand + first_block_ * BlockSize();
        ZX_DEBUG_ASSERT(request.offset_nand < NumPages());
        ZX_DEBUG_ASSERT(request.offset_nand + request.length <= NumPages());
    }
    ASSERT_EQ(ZX_OK, zx_handle_duplicate(vmo.GetVmo(), ZX_RIGHT_SAME_RIGHTS, &request_copy.vmo));
    ASSERT_EQ(sizeof(response), ioctl_nand_broker_read(get(), &request_copy, response));
    END_TEST;
}

bool NandDevice::Write(const fzl::MappedVmo& vmo, const nand_broker_request_t& request,
                       nand_broker_response_t* response) {
    BEGIN_TEST;
    nand_broker_request_t request_copy = request;
    if (!full_device_) {
        request_copy.offset_nand = request.offset_nand + first_block_ * BlockSize();
        ZX_DEBUG_ASSERT(request.offset_nand < NumPages());
        ZX_DEBUG_ASSERT(request.offset_nand + request.length <= NumPages());
    }
    ASSERT_EQ(ZX_OK, zx_handle_duplicate(vmo.GetVmo(), ZX_RIGHT_SAME_RIGHTS, &request_copy.vmo));
    ASSERT_EQ(sizeof(response), ioctl_nand_broker_write(get(), &request_copy, response));
    END_TEST;
}

bool NandDevice::Erase(const nand_broker_request_t& request, nand_broker_response_t* response) {
    BEGIN_TEST;
    nand_broker_request_t request_copy = request;
    if (!full_device_) {
        request_copy.offset_nand = request.offset_nand + first_block_;
        ZX_DEBUG_ASSERT(request.offset_nand < NumBlocks());
        ZX_DEBUG_ASSERT(request.offset_nand + request.length <= NumBlocks());
    }
    ASSERT_EQ(sizeof(response), ioctl_nand_broker_erase(get(), &request_copy, response));
    END_TEST;
}

bool NandDevice::EraseBlock(uint32_t block_num) {
    BEGIN_TEST;
    nand_broker_request_t request = {};
    nand_broker_response_t response = {};

    request.length = 1;
    request.offset_nand = block_num;
    ASSERT_TRUE(Erase(request, &response));
    EXPECT_EQ(ZX_OK, response.status);
    END_TEST;
}

bool NandDevice::CheckPattern(uint8_t expected, int start, int num_pages, const void* memory) {
    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(memory) + PageSize() * start;
    for (uint32_t i = 0; i < PageSize() * num_pages; i++) {
        if (buffer[i] != expected) {
            return false;
        }
    }
    return true;
}

bool NandDevice::ValidateNandDevice() {
    if (parent_->IsExternal()) {
        // This looks like using code under test to setup the test, but this
        // path is for external devices, not really the broker. The issue is that
        // ParentDevice cannot query a nand device for the actual parameters.
        nand_info_t info;
        if (ioctl_nand_broker_get_info(get(), &info) < 0) {
            printf("Failed to query nand device\n");
            return false;
        }
        parent_->SetInfo(info);
    }

    num_blocks_ = parent_->NumBlocks();
    first_block_ = parent_->FirstBlock();
    if (OobSize() < kMinOobSize || BlockSize() < kMinBlockSize || num_blocks_ < kMinNumBlocks ||
        num_blocks_ + first_block_ > parent_->Info().num_blocks) {
        printf("Invalid nand device parameters\n");
        return false;
    }
    if (num_blocks_ != parent_->Info().num_blocks) {
        // Not using the whole device, don't need to test all limits.
        num_blocks_ = fbl::min(num_blocks_, kMinNumBlocks);
        full_device_ = false;
    }
    return true;
}

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    NandDevice device;

    ASSERT_TRUE(device.IsValid());
    END_TEST;
}

bool QueryTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());

    nand_info_t info;
    ASSERT_EQ(sizeof(info), ioctl_nand_broker_get_info(device.get(), &info));

    EXPECT_EQ(device.Info().page_size, info.page_size);
    EXPECT_EQ(device.Info().oob_size, info.oob_size);
    EXPECT_EQ(device.Info().pages_per_block, info.pages_per_block);
    EXPECT_EQ(device.Info().num_blocks, info.num_blocks);
    EXPECT_EQ(device.Info().ecc_bits, info.ecc_bits);
    EXPECT_EQ(device.Info().nand_class, info.nand_class);

    END_TEST;
}

bool ReadWriteLimitsTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());

    nand_broker_request_t request = {};
    nand_broker_response_t response = {};
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, ioctl_nand_broker_read(device.get(), &request, &response));
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, ioctl_nand_broker_write(device.get(), &request, &response));

    fbl::unique_ptr<fzl::MappedVmo> vmo;
    ASSERT_EQ(ZX_OK, fzl::MappedVmo::Create(device.MaxBufferSize(), nullptr, &vmo));

    ASSERT_TRUE(device.Read(*vmo, request, &response));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);

    ASSERT_TRUE(device.Write(*vmo, request, &response));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);

    if (device.IsFullDevice()) {
        request.length = 1;
        request.offset_nand = device.NumPages();

        ASSERT_TRUE(device.Read(*vmo, request, &response));
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);

        ASSERT_TRUE(device.Write(*vmo, request, &response));
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);

        request.length = 2;
        request.offset_nand = device.NumPages() - 1;

        ASSERT_TRUE(device.Read(*vmo, request, &response));
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);

        ASSERT_TRUE(device.Write(*vmo, request, &response));
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);
    }

    request.length = 1;
    request.offset_nand = device.NumPages() - 1;

    ASSERT_TRUE(device.Read(*vmo, request, &response));
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, response.status);

    ASSERT_TRUE(device.Write(*vmo, request, &response));
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, response.status);

    request.data_vmo = true;

    ASSERT_TRUE(device.Read(*vmo, request, &response));
    EXPECT_EQ(ZX_OK, response.status);

    ASSERT_TRUE(device.Write(*vmo, request, &response));
    EXPECT_EQ(ZX_OK, response.status);

    END_TEST;
}

bool EraseLimitsTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());

    nand_broker_request_t request = {};
    nand_broker_response_t response = {};
    ASSERT_TRUE(device.Erase(request, &response));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);

    request.offset_nand = device.NumBlocks();

    if (device.IsFullDevice()) {
        request.length = 1;
        ASSERT_TRUE(device.Erase(request, &response));
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);

        request.length = 2;
        request.offset_nand = device.NumBlocks() - 1;
        ASSERT_TRUE(device.Erase(request, &response));
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);
    }

    request.length = 1;
    request.offset_nand = device.NumBlocks() - 1;
    ASSERT_TRUE(device.Erase(request, &response));
    EXPECT_EQ(ZX_OK, response.status);

    END_TEST;
}

bool ReadWriteTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());
    ASSERT_TRUE(device.EraseBlock(0));

    nand_broker_request_t request = {};
    nand_broker_response_t response = {};
    fbl::unique_ptr<fzl::MappedVmo> vmo;
    ASSERT_EQ(ZX_OK, fzl::MappedVmo::Create(device.MaxBufferSize(), nullptr, &vmo));

    memset(vmo->GetData(), 0x55, vmo->GetSize());

    request.length = 4;
    request.offset_nand = 4;
    request.data_vmo = true;

    ASSERT_TRUE(device.Write(*vmo, request, &response));

    memset(vmo->GetData(), 0, vmo->GetSize());

    ASSERT_TRUE(device.Read(*vmo, request, &response));

    ASSERT_EQ(0, response.corrected_bit_flips);
    ASSERT_TRUE(device.CheckPattern(0x55, 0, 4, vmo->GetData()));

    END_TEST;
}

bool ReadWriteOobTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());
    ASSERT_TRUE(device.EraseBlock(0));

    nand_broker_request_t request = {};
    nand_broker_response_t response = {};
    fbl::unique_ptr<fzl::MappedVmo> vmo;
    ASSERT_EQ(ZX_OK, fzl::MappedVmo::Create(device.MaxBufferSize(), nullptr, &vmo));

    const char desired[] = {'a', 'b', 'c', 'd'};
    memcpy(vmo->GetData(), desired, sizeof(desired));

    request.length = 1;
    request.offset_nand = 2;
    request.oob_vmo = true;

    ASSERT_TRUE(device.Write(*vmo, request, &response));

    request.length = 2;
    request.offset_nand = 1;
    memset(vmo->GetData(), 0, device.OobSize() * 2);

    ASSERT_TRUE(device.Read(*vmo, request, &response));
    ASSERT_EQ(0, response.corrected_bit_flips);

    // The "second page" has the data of interest.
    ASSERT_EQ(0,
              memcmp(reinterpret_cast<char*>(vmo->GetData()) + device.OobSize(), desired,
                     sizeof(desired)));

    END_TEST;
}

bool ReadWriteDataAndOobTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());
    ASSERT_TRUE(device.EraseBlock(0));

    nand_broker_request_t request = {};
    nand_broker_response_t response = {};
    fbl::unique_ptr<fzl::MappedVmo> vmo;
    ASSERT_EQ(ZX_OK, fzl::MappedVmo::Create(device.MaxBufferSize(), nullptr, &vmo));

    char* buffer = reinterpret_cast<char*>(vmo->GetData());
    memset(buffer, 0x55, device.PageSize() * 2);
    memset(buffer + device.PageSize() * 2, 0xaa, device.OobSize() * 2);

    request.length = 2;
    request.offset_nand = 2;
    request.offset_oob_vmo = 2; // OOB is right after data.
    request.data_vmo = true;
    request.oob_vmo = true;

    ASSERT_TRUE(device.Write(*vmo, request, &response));

    memset(buffer, 0, device.PageSize() * 4);

    ASSERT_TRUE(device.Read(*vmo, request, &response));
    ASSERT_EQ(0, response.corrected_bit_flips);

    // Verify data.
    ASSERT_TRUE(device.CheckPattern(0x55, 0, 2, buffer));

    // Verify OOB.
    memset(buffer, 0xaa, device.PageSize());
    ASSERT_EQ(0, memcmp(buffer + device.PageSize() * 2, buffer, device.OobSize() * 2));

    END_TEST;
}

bool EraseTest() {
    BEGIN_TEST;
    NandDevice device;
    ASSERT_TRUE(device.IsValid());

    nand_broker_request_t request = {};
    nand_broker_response_t response = {};
    fbl::unique_ptr<fzl::MappedVmo> vmo;
    ASSERT_EQ(ZX_OK, fzl::MappedVmo::Create(device.MaxBufferSize(), nullptr, &vmo));

    memset(vmo->GetData(), 0x55, vmo->GetSize());

    request.length = kMinBlockSize;
    request.data_vmo = true;
    request.offset_nand = device.BlockSize();
    ASSERT_TRUE(device.Write(*vmo, request, &response));

    request.offset_nand = device.BlockSize() * 2;
    ASSERT_TRUE(device.Write(*vmo, request, &response));

    ASSERT_TRUE(device.EraseBlock(1));
    ASSERT_TRUE(device.EraseBlock(2));

    ASSERT_TRUE(device.Read(*vmo, request, &response));
    ASSERT_TRUE(device.CheckPattern(0xff, 0, kMinBlockSize, vmo->GetData()));

    request.offset_nand = device.BlockSize();
    ASSERT_TRUE(device.Read(*vmo, request, &response));
    ASSERT_TRUE(device.CheckPattern(0xff, 0, kMinBlockSize, vmo->GetData()));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(NandBrokerTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(QueryTest)
RUN_TEST_SMALL(ReadWriteLimitsTest)
RUN_TEST_SMALL(EraseLimitsTest)
RUN_TEST_SMALL(ReadWriteTest)
RUN_TEST_SMALL(ReadWriteOobTest)
RUN_TEST_SMALL(ReadWriteDataAndOobTest)
RUN_TEST_SMALL(EraseTest)
END_TEST_CASE(NandBrokerTests)
