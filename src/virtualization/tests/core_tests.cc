// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <cstdlib>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <re2/re2.h>

#include "src/lib/json_parser/json_parser.h"
#include "src/virtualization/tests/lib/guest_test.h"

namespace {

using testing::HasSubstr;
using namespace std::chrono_literals;

constexpr size_t kVirtioConsoleMessageCount = 100;
constexpr char kVirtioRngUtil[] = "virtio_rng_test_util";

constexpr uint64_t kOneKibibyte = 1ul << 10;
constexpr uint64_t kOneMebibyte = 1ul << 20;
constexpr uint64_t kOneGibibyte = 1ul << 30;

// Memory tests moderately increase the VM's guest memory above the default so that they can
// validate that the guest memory is configurable.
#if __aarch64__
constexpr uint64_t kGuestMemoryForMemoryTests = kOneGibibyte + 512 * kOneMebibyte;
#else
constexpr uint64_t kGuestMemoryForMemoryTests = 4 * kOneGibibyte + 512 * kOneMebibyte;
#endif

template <class T>
using CoreGuestTest = GuestTest<T>;

// This test suite contains all guest tests that don't require a specific configuration of devices.
// They are grouped together so that they share guests and reduce the number of times guests are
// started, which is time consuming. Note that this means that some tests need to dynamically check
// the guest type in order to skip under certain conditions.
TYPED_TEST_SUITE(CoreGuestTest, AllGuestTypes, GuestTestNameGenerator);

TYPED_TEST(CoreGuestTest, VirtioConsole) {
  // Test many small packets.
  std::string result;
  for (size_t i = 0; i != kVirtioConsoleMessageCount; ++i) {
    EXPECT_EQ(this->Execute({"echo", "test"}, &result), ZX_OK);
    EXPECT_EQ(result, "test\n");
  }

  // Test large packets. Note that we must keep the total length below 4096,
  // which is the maximum line length for dash.
  std::string test_data = "";
  for (size_t i = 0; i != kVirtioConsoleMessageCount; ++i) {
    test_data.append("Lorem ipsum dolor sit amet consectetur");
  }
  EXPECT_EQ(this->Execute({"echo", test_data.c_str()}, &result), ZX_OK);
  test_data.append("\n");
  EXPECT_EQ(result, test_data);
}

TYPED_TEST(CoreGuestTest, VirtioRng) {
  std::string result;
  ASSERT_EQ(this->RunUtil(kVirtioRngUtil, {}, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TYPED_TEST(CoreGuestTest, RealTimeClock) {
  // Real time clock not functioning in Zircon guest at this time.
  //
  // TODO(fxbug.dev/75440): Fix clock in Zircon guest.
  if (this->GetGuestKernel() == GuestKernel::ZIRCON) {
    return;
  }

  // Print seconds since Unix epoch (1970-01-01), and parse the result.
  std::string result;
  ASSERT_EQ(this->Execute({"/bin/date", "+%s"}, {}, &result), ZX_OK);
  int64_t guest_timestamp = std::stol(result, /*pos=*/nullptr, /*base=*/10);
  ASSERT_TRUE(guest_timestamp > 0) << "Could not parse guest time.";

  // Get the system time.
  std::chrono::time_point now = std::chrono::system_clock::now();
  int64_t host_timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

  // Ensure the clock matches the system time, within a few minutes.
  std::cout << "Guest time is " << (host_timestamp - guest_timestamp)
            << " second(s) behind host time.\n";
  EXPECT_LT(std::abs(host_timestamp - guest_timestamp),
            std::chrono::duration_cast<std::chrono::seconds>(5min).count())
      << "Guest time (" << guest_timestamp << ") and host time (" << host_timestamp
      << ") differ by more than 5 minutes.";
}

template <typename T>
class CustomizableMemoryGuest : public T {
 public:
  explicit CustomizableMemoryGuest(async::Loop& loop) : T(loop) {}

  zx_status_t BuildLaunchInfo(GuestLaunchInfo* launch_info) override {
    zx_status_t status = T::BuildLaunchInfo(launch_info);
    if (status != ZX_OK) {
      return status;
    }

    launch_info->config.set_guest_memory(kGuestMemoryForMemoryTests);

    return ZX_OK;
  }
};

using LinuxCustomizableMemoryGuestTypes =
    ::testing::Types<CustomizableMemoryGuest<DebianEnclosedGuest>,
                     CustomizableMemoryGuest<TerminaEnclosedGuest>>;

template <class T>
using CustomizableMemoryLinuxGuestTest = GuestTest<T>;
TYPED_TEST_SUITE(CustomizableMemoryLinuxGuestTest, LinuxCustomizableMemoryGuestTypes,
                 GuestTestNameGenerator);

TYPED_TEST(CustomizableMemoryLinuxGuestTest, LinuxSystemMemoryConfigurable) {
  std::string result;
  ASSERT_EQ(this->Execute({"cat", "/proc/meminfo"}, {}, &result), ZX_OK);

  std::string sizestr;
  RE2::PartialMatch(result, R"(MemTotal:\s+(\d+)\skB)", &sizestr);

  uint64_t system_memory = std::stoull(sizestr) * kOneKibibyte;

  // Linux doesn't report the actual amount of system memory via meminfo, and we don't
  // currently emulate SMBIOS allowing us to use dmidecode. For now, we can just assume
  // that the Linux kernel isn't taking up more than 300 mebibytes giving moderate
  // confidence that this works.
  uint64_t memory_leeway = 300 * kOneMebibyte;
  EXPECT_THAT(system_memory + memory_leeway, ::testing::Gt(kGuestMemoryForMemoryTests));
}

using CustomizableMemoryZirconGuestTest = GuestTest<CustomizableMemoryGuest<ZirconEnclosedGuest>>;
TEST_F(CustomizableMemoryZirconGuestTest, ZirconSystemMemoryConfigurable) {
  std::string result;
  ASSERT_EQ(this->Execute({"memgraph"}, {}, &result), ZX_OK);

  json_parser::JSONParser parser;
  rapidjson::Document memgraph = parser.ParseFromString(result, "memgraph");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();
  ASSERT_TRUE(memgraph.IsArray());

  uint64_t system_memory = 0;
  std::string physmem("physmem");
  for (auto& entry : memgraph.GetArray()) {
    if (entry.HasMember("name") && entry["name"].GetString() == physmem) {
      system_memory = entry["size_bytes"].GetUint64();
    }
  }

  ASSERT_NE(0ul, system_memory) << "Couldn't find physmem entry in memgraph output";

  // Zircon may or may not allow the first MiB to be used for as guest memory, so expect that
  // reported memory is within one MiB of expected memory.
  uint64_t memory_leeway = kOneMebibyte;
  EXPECT_THAT(system_memory + memory_leeway, ::testing::Gt(kGuestMemoryForMemoryTests));
}

}  // namespace
