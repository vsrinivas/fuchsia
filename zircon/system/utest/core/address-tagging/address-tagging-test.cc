// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/features.h>
#include <zircon/syscalls.h>

#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

namespace {

#if defined(__aarch64__)

constexpr size_t kTagShift = 56;
constexpr uint8_t kTestTag = 0xAB;

constexpr uint64_t AddTag(uintptr_t ptr, uint8_t tag) {
  [[maybe_unused]] constexpr uint64_t kTagMask = UINT64_C(0xff) << kTagShift;
  assert((kTagMask & ptr) == 0 && "Expected an untagged pointer.");
  return (static_cast<uint64_t>(tag) << kTagShift) | static_cast<uint64_t>(ptr);
}

template <typename T>
T* AddTag(T* ptr, uint8_t tag) {
  return reinterpret_cast<T*>(AddTag(reinterpret_cast<uintptr_t>(ptr), tag));
}

// Disable sanitizers for this because any sanitizer that involves doing a
// right shift to get a shadow memory location could cause a tag to leak into
// bit 55, leading to an incorrect shadow being referenced. This will affect
// ASan and eventually HWASan.
#ifdef __clang__
[[clang::no_sanitize("all")]]
#endif
void DerefTaggedPtr(int* ptr) {
  *ptr = 1;
}

TEST(TopByteIgnoreTests, AddressTaggingGetSystemFeaturesAArch64) {
  uint32_t features = 0;
  ASSERT_OK(zx_system_get_features(ZX_FEATURE_KIND_ADDRESS_TAGGING, &features));
  ASSERT_EQ(features, ZX_ARM64_FEATURE_ADDRESS_TAGGING_TBI);

  // Since TBI is supported, we can access tagged pointers.
  int val = 0;
  DerefTaggedPtr(AddTag(&val, kTestTag));
  ASSERT_EQ(val, 1);
}

#elif defined(__x86_64__)

TEST(TopByteIgnoreTests, AddressTaggingGetSystemFeaturesX86_64) {
  uint32_t features = 0;
  ASSERT_OK(zx_system_get_features(ZX_FEATURE_KIND_ADDRESS_TAGGING, &features));
  ASSERT_EQ(features, 0);
}

#endif

}  // namespace
