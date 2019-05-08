// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/vmo-buffer.h>
#include <lib/zx/vmo.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

const vmoid_t kGoldenVmoid = 5;
const size_t kCapacity = 3;
constexpr char kGoldenLabel[] = "test-vmo";

// TODO(ZX-4003): This interface is larger than necessary. Can we reduce it
// to just "attach/detach vmo"?
class MockSpaceManager : public SpaceManager {
public:
    bool detached() const { return detached_; }

private:
    const Superblock& Info() const final {
        ZX_ASSERT_MSG(false, "Test should not invoke function: %s\n", __FUNCTION__);
    }

    zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) override {
        *out = kGoldenVmoid;
        return ZX_OK;
    }

    zx_status_t DetachVmo(vmoid_t vmoid) final {
        EXPECT_EQ(kGoldenVmoid, vmoid);
        EXPECT_FALSE(detached_);
        detached_ = true;
        return ZX_OK;
    }

    zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final {
        ZX_ASSERT_MSG(false, "Test should not invoke function: %s\n", __FUNCTION__);
    }

    zx_status_t AddBlocks(size_t nblocks, RawBitmap* block_map) final {
        ZX_ASSERT_MSG(false, "Test should not invoke function: %s\n", __FUNCTION__);
    }

    bool detached_ = false;
};

TEST(VmoBufferTest, EmptyTest) {
    VmoBuffer buffer;
    EXPECT_EQ(0, buffer.capacity());
    EXPECT_EQ(VMOID_INVALID, buffer.vmoid());
}

TEST(VmoBufferTest, TestLabel) {
    class MockManager : public MockSpaceManager {
        zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final {
            char name[ZX_MAX_NAME_LEN];
            EXPECT_OK(vmo.get_property(ZX_PROP_NAME, name, sizeof(name)));
            EXPECT_STR_EQ(kGoldenLabel, name);
            *out = kGoldenVmoid;
            return ZX_OK;
        }
    } manager;

    VmoBuffer buffer;
    ASSERT_OK(buffer.Initialize(&manager, kCapacity, kGoldenLabel));
}

TEST(VmoBufferTest, VmoidRegistration) {
    MockSpaceManager manager;
    {
        VmoBuffer buffer;
        ASSERT_OK(buffer.Initialize(&manager, kCapacity, kGoldenLabel));
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());

        EXPECT_FALSE(manager.detached());
    }
    EXPECT_TRUE(manager.detached());
}

TEST(VmoBufferTest, MoveConstructorTest) {
    MockSpaceManager manager;

    {
        VmoBuffer buffer;
        ASSERT_OK(buffer.Initialize(&manager, kCapacity, kGoldenLabel));
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());

        VmoBuffer move_constructed(std::move(buffer));
        EXPECT_EQ(kCapacity, move_constructed.capacity());
        EXPECT_EQ(kGoldenVmoid, move_constructed.vmoid());
        EXPECT_FALSE(manager.detached());
    }
    EXPECT_TRUE(manager.detached());
}

TEST(VmoBufferTest, MoveAssignmentTest) {
    MockSpaceManager manager;

    {
        VmoBuffer buffer;
        ASSERT_OK(buffer.Initialize(&manager, kCapacity, kGoldenLabel));
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());

        auto buffer2 = std::move(buffer);
        EXPECT_EQ(kCapacity, buffer2.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer2.vmoid());
        EXPECT_FALSE(manager.detached());
    }
    EXPECT_TRUE(manager.detached());
}

TEST(VmoBufferTest, MoveToSelfTest) {
    MockSpaceManager manager;

    {
        VmoBuffer buffer;
        ASSERT_OK(buffer.Initialize(&manager, kCapacity, kGoldenLabel));
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());

        VmoBuffer* addr = &buffer;
        *addr = std::move(buffer);
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());
        EXPECT_FALSE(manager.detached());
    }
    EXPECT_TRUE(manager.detached());
}

TEST(VmoBufferTest, MappingTest) {
    MockSpaceManager manager;

    VmoBuffer buffer;
    ASSERT_OK(buffer.Initialize(&manager, kCapacity, kGoldenLabel));
    char buf[kBlobfsBlockSize];
    memset(buf, 'a', sizeof(buf));

    for (size_t i = 0; i < kCapacity; i++) {
        memcpy(buffer.MutableData(i), buf, kBlobfsBlockSize);
    }
    for (size_t i = 0; i < kCapacity; i++) {
        EXPECT_EQ(0, memcmp(buf, buffer.MutableData(i), kBlobfsBlockSize));
    }
}

TEST(VmoBufferTest, CompareVmoToMapping) {
    MockSpaceManager manager;
    VmoBuffer buffer;
    ASSERT_OK(buffer.Initialize(&manager, kCapacity, kGoldenLabel));

    // Fill |buffer| with some arbitrary data via mapping.
    char buf[kBlobfsBlockSize * 3];
    memset(buf, 'a', sizeof(buf));
    for (size_t i = 0; i < kCapacity; i++) {
        memcpy(buffer.MutableData(i), buf, kBlobfsBlockSize);
    }

    // Check that we can read from the VMO directly.
    ASSERT_OK(buffer.vmo().read(buf, 0, kCapacity * kBlobfsBlockSize));

    // The data from the VMO is equivalent to the data from the mapping.
    EXPECT_EQ(0, memcmp(buf, buffer.MutableData(0), kCapacity * kBlobfsBlockSize));
}

} // namespace
} // namespace blobfs
