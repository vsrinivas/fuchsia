// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/vmo.h>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

TEST(FakeDdk, InspectVmoLeak) {
  fake_ddk::Bind bind;

  zx::vmo inspect_vmo;
  ASSERT_OK(zx::vmo::create(4096u, 0, &inspect_vmo));

  zx::vmo dup_vmo;
  ASSERT_OK(inspect_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo));

  ddk::DeviceAddArgs args("test-driver");
  args.set_inspect_vmo(std::move(dup_vmo));
  device_add_args_t device_args = args.get();

  zx_device_t* device;
  EXPECT_OK(device_add(fake_ddk::kFakeParent, &device_args, &device));

  device_async_remove(device);
  EXPECT_TRUE(bind.Ok());

  zx_info_handle_count_t count;
  ASSERT_OK(inspect_vmo.get_info(ZX_INFO_HANDLE_COUNT, &count, sizeof(count), nullptr, nullptr));

  // |inspect_vmo| should be the only handle.
  EXPECT_EQ(1u, count.handle_count);
}

TEST(FakeDdk, SetMetadata) {
  fake_ddk::Bind bind;

  // Can't get metadata to begin.
  char buf[10] = {};
  size_t actual = 0;
  size_t size = 0;
  ASSERT_NE(device_get_metadata(nullptr, 42, buf, sizeof(buf), &actual), ZX_OK);
  ASSERT_NE(device_get_metadata_size(nullptr, 42, &size), ZX_OK);

  const char kSource[] = "test";
  bind.SetMetadata(42, kSource, sizeof(kSource));

  // Can get metadata with correct type after setting.
  ASSERT_OK(device_get_metadata(nullptr, 42, buf, sizeof(buf), &actual));
  ASSERT_EQ(actual, sizeof(kSource));
  ASSERT_BYTES_EQ(buf, kSource, sizeof(kSource));
  ASSERT_OK(device_get_metadata_size(nullptr, 42, &size));
  ASSERT_EQ(size, sizeof(kSource));

  // Can't get metadata with incorrect type after setting.
  ASSERT_NE(device_get_metadata(nullptr, 1, buf, sizeof(buf), &actual), ZX_OK);
  ASSERT_NE(device_get_metadata_size(nullptr, 1, &size), ZX_OK);

  const char kSource2[] = "other";
  bind.SetMetadata(1, kSource2, sizeof(kSource2));

  // We can get it after setting it though
  ASSERT_OK(device_get_metadata(nullptr, 1, buf, sizeof(buf), &actual));
  ASSERT_EQ(actual, sizeof(kSource2));
  ASSERT_BYTES_EQ(buf, kSource2, sizeof(kSource2));
  ASSERT_OK(device_get_metadata_size(nullptr, 1, &size));
  ASSERT_EQ(size, sizeof(kSource2));

  // Original metadata still works too
  ASSERT_OK(device_get_metadata(nullptr, 42, buf, sizeof(buf), &actual));
  ASSERT_EQ(actual, sizeof(kSource));
  ASSERT_BYTES_EQ(buf, kSource, sizeof(kSource));
  ASSERT_OK(device_get_metadata_size(nullptr, 42, &size));
  ASSERT_EQ(size, sizeof(kSource));
}

class CompositeTest : public zxtest::Test {
 public:
  ~CompositeTest() override = default;
  void SetUp() override {
    fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[2], 2);
    fragments[0].name = "fragment-1";
    fragments[0].protocols.emplace_back(
        fake_ddk::ProtocolEntry{0, fake_ddk::Protocol{nullptr, nullptr}});
    fragments[0].protocols.emplace_back(
        fake_ddk::ProtocolEntry{1, fake_ddk::Protocol{nullptr, nullptr}});
    fragments[1].name = "fragment-2";
    fragments[1].protocols.emplace_back(
        fake_ddk::ProtocolEntry{2, fake_ddk::Protocol{nullptr, nullptr}});

    bind.SetFragments(std::move(fragments));
  }

 private:
  fake_ddk::Bind bind;
};

TEST_F(CompositeTest, GetFragmentCount) {
  EXPECT_EQ(device_get_fragment_count(fake_ddk::FakeParent()), 2);
}

TEST_F(CompositeTest, GetProtocolParent) {
  // Protocols are not available under parent device.
  fake_ddk::Protocol proto = {};
  EXPECT_NE(device_get_protocol(fake_ddk::FakeParent(), 0, &proto), ZX_OK);
  EXPECT_NE(device_get_protocol(fake_ddk::FakeParent(), 1, &proto), ZX_OK);
  EXPECT_NE(device_get_protocol(fake_ddk::FakeParent(), 2, &proto), ZX_OK);
}

TEST_F(CompositeTest, GetFragment) {
  zx_device_t *fragment1, *fragment2;
  EXPECT_TRUE(device_get_fragment(fake_ddk::FakeParent(), "fragment-1", &fragment1));
  EXPECT_TRUE(device_get_fragment(fake_ddk::FakeParent(), "fragment-2", &fragment2));

  // Only first two protocols are availably in fragment-1.
  fake_ddk::Protocol proto = {};
  EXPECT_OK(device_get_protocol(fragment1, 0, &proto));
  EXPECT_OK(device_get_protocol(fragment1, 1, &proto));
  EXPECT_NE(device_get_protocol(fragment1, 2, &proto), ZX_OK);

  // Only last protocols is availably in fragment-2.
  EXPECT_NE(device_get_protocol(fragment2, 0, &proto), ZX_OK);
  EXPECT_NE(device_get_protocol(fragment2, 1, &proto), ZX_OK);
  EXPECT_OK(device_get_protocol(fragment2, 2, &proto));
}

TEST_F(CompositeTest, GetFragments) {
  composite_device_fragment_t fragments[2] = {};
  size_t actual;
  device_get_fragments(fake_ddk::FakeParent(), fragments, 2, &actual);
  EXPECT_EQ(actual, 2);

  // Fragment names line up
  EXPECT_EQ(strcmp(fragments[0].name, "fragment-1"), 0);
  EXPECT_EQ(strcmp(fragments[1].name, "fragment-2"), 0);

  // Only first two protocols are availably in fragment-1.
  fake_ddk::Protocol proto = {};
  EXPECT_OK(device_get_protocol(fragments[0].device, 0, &proto));
  EXPECT_OK(device_get_protocol(fragments[0].device, 1, &proto));
  EXPECT_NE(device_get_protocol(fragments[0].device, 2, &proto), ZX_OK);

  // Only last protocols is availably in fragment-2.
  EXPECT_NE(device_get_protocol(fragments[1].device, 0, &proto), ZX_OK);
  EXPECT_NE(device_get_protocol(fragments[1].device, 1, &proto), ZX_OK);
  EXPECT_OK(device_get_protocol(fragments[1].device, 2, &proto));
}
