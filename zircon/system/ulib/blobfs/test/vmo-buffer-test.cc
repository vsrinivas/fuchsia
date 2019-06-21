// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/vmo-buffer.h>

#include <blobfs/format.h>
#include <lib/zx/vmo.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

const vmoid_t kGoldenVmoid = 5;
const size_t kCapacity = 3;
constexpr char kGoldenLabel[] = "test-vmo";

class MockVmoidRegistry : public VmoidRegistry {
public:
    bool detached() const { return detached_; }

private:
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

    bool detached_ = false;
};

TEST(VmoBufferTest, EmptyTest) {
    VmoBuffer buffer;
    EXPECT_EQ(0, buffer.capacity());
    EXPECT_EQ(VMOID_INVALID, buffer.vmoid());
}

TEST(VmoBufferTest, TestLabel) {
    class MockRegistry : public MockVmoidRegistry {
        zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final {
            char name[ZX_MAX_NAME_LEN];
            EXPECT_OK(vmo.get_property(ZX_PROP_NAME, name, sizeof(name)));
            EXPECT_STR_EQ(kGoldenLabel, name);
            *out = kGoldenVmoid;
            return ZX_OK;
        }
    } registry;

    VmoBuffer buffer;
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kGoldenLabel));
}

TEST(VmoBufferTest, VmoidRegistration) {
    MockVmoidRegistry registry;
    {
        VmoBuffer buffer;
        ASSERT_OK(buffer.Initialize(&registry, kCapacity, kGoldenLabel));
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());

        EXPECT_FALSE(registry.detached());
    }
    EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MoveConstructorTest) {
    MockVmoidRegistry registry;

    {
        VmoBuffer buffer;
        ASSERT_OK(buffer.Initialize(&registry, kCapacity, kGoldenLabel));
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());

        VmoBuffer move_constructed(std::move(buffer));
        EXPECT_EQ(kCapacity, move_constructed.capacity());
        EXPECT_EQ(kGoldenVmoid, move_constructed.vmoid());
        EXPECT_FALSE(registry.detached());
    }
    EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MoveAssignmentTest) {
    MockVmoidRegistry registry;

    {
        VmoBuffer buffer;
        ASSERT_OK(buffer.Initialize(&registry, kCapacity, kGoldenLabel));
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());

        auto buffer2 = std::move(buffer);
        EXPECT_EQ(kCapacity, buffer2.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer2.vmoid());
        EXPECT_FALSE(registry.detached());
    }
    EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MoveToSelfTest) {
    MockVmoidRegistry registry;

    {
        VmoBuffer buffer;
        ASSERT_OK(buffer.Initialize(&registry, kCapacity, kGoldenLabel));
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());

        VmoBuffer* addr = &buffer;
        *addr = std::move(buffer);
        EXPECT_EQ(kCapacity, buffer.capacity());
        EXPECT_EQ(kGoldenVmoid, buffer.vmoid());
        EXPECT_FALSE(registry.detached());
    }
    EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MappingTest) {
    MockVmoidRegistry registry;

    VmoBuffer buffer;
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kGoldenLabel));
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
    MockVmoidRegistry registry;
    VmoBuffer buffer;
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kGoldenLabel));

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
