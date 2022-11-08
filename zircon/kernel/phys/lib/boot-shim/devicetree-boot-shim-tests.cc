// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/devicetree-boot-shim.h>
#include <lib/boot-shim/item-base.h>
#include <lib/devicetree/devicetree.h>
#include <lib/devicetree/matcher-result.h>
#include <lib/devicetree/matcher.h>
#include <lib/devicetree/path.h>
#include <lib/stdcompat/span.h>
#include <lib/zbitl/image.h>
#include <zircon/boot/image.h>

#include <array>
#include <cstdint>

#include <zxtest/zxtest.h>

#include "zircon/kernel/lib/devicetree/tests/test_helper.h"

namespace {

class DevicetreeItem1
    : public boot_shim::SingleOptionalItem<std::array<char, 30>, ZBI_TYPE_CMDLINE> {
 public:
  auto GetInitMatcher() {
    return [this](const devicetree::NodePath& path, devicetree::Properties props,
                  const devicetree::PathResolver& resolver) -> devicetree::MatcherScanResult<1> {
      constexpr std::string_view kPath = "bar/G/H";
      auto res = resolver.Resolve(kPath);

      if (!res.is_ok()) {
        if (res.error_value() == devicetree::PathResolver::ResolveError::kBadAlias) {
          return {devicetree::MatcherResult::kAvoidSubtree};
        }
        return {devicetree::MatcherResult::kNeedsAliases};
      }

      switch (devicetree::ComparePath(path, *res)) {
        case devicetree::kIsAncestor:
          return {devicetree::MatcherResult::kVisitSubtree};
        case devicetree::kIsMatch: {
          std::array<char, 30> msg = {};
          count++;
          cmdline_ = "--visit-count=" + std::to_string(count);
          memcpy(msg.data(), cmdline_.data(), cmdline_.size());
          this->set_payload(msg);
          return {devicetree::MatcherResult::kDone};
        }
        default:
          return {devicetree::MatcherResult::kAvoidSubtree};
      }
    };
  }

 private:
  std::string cmdline_;
  int count;
};

static_assert(boot_shim::internal::HasGetInitMatcher_v<DevicetreeItem1>);
static_assert(boot_shim::internal::IsDevicetreeItem<DevicetreeItem1>::value);

class DevicetreeItem2
    : public boot_shim::SingleOptionalItem<std::array<char, 30>, ZBI_TYPE_CMDLINE> {
 public:
  auto GetInitMatcher() {
    return [this](const devicetree::NodePath& path,
                  devicetree::Properties props) -> devicetree::MatcherScanResult<2> {
      constexpr std::string_view kPath = "/E/F/G/H";

      switch (devicetree::ComparePath(path, kPath)) {
        case devicetree::kIsAncestor:
          return {devicetree::MatcherResult::kVisitSubtree};
        case devicetree::kIsMatch: {
          std::array<char, 30> msg = {};
          count++;
          if (count == 1) {
            return {devicetree::MatcherResult::kAvoidSubtree};
          }
          cmdline_ = "--visit-count-b=" + std::to_string(count);
          memcpy(msg.data(), cmdline_.data(), cmdline_.size());
          this->set_payload(msg);
          return {devicetree::MatcherResult::kDone};
        }
        default:
          return {devicetree::MatcherResult::kAvoidSubtree};
      }
    };
  }

 private:
  std::string cmdline_;
  int count;
};

static_assert(boot_shim::internal::HasGetInitMatcher_v<DevicetreeItem2>);
static_assert(boot_shim::internal::IsDevicetreeItem<DevicetreeItem2>::value);

constexpr size_t kMaxSize = 1024;
class DevicetreeBootShimTest : public zxtest::Test {
 public:
  static void SetUpTestSuite() {
    ASSERT_NO_FATAL_FAILURE(ReadTestData("complex_with_alias_first.dtb", fdt_));
  }

  /*
          *
     /     / \
   aliases A   E
          / \   \
         B   C   F
            /   / \
           D   G   I
              /
             H

 aliases:
  foo = /A/C
  bar = /E/F
*/
  devicetree::Devicetree fdt() { return devicetree::Devicetree({fdt_.data(), fdt_.size()}); }

 private:
  static std::array<uint8_t, kMaxSize> fdt_;
};

std::array<uint8_t, kMaxSize> DevicetreeBootShimTest::fdt_;

void CheckZbiHasItemWithContent(zbitl::Image<cpp20::span<std::byte>> image, uint32_t item_type,
                                std::string_view contents) {
  int count = 0;
  for (auto it : image) {
    auto [h, p] = it;

    EXPECT_EQ(h->type, ZBI_TYPE_CMDLINE);
    EXPECT_EQ(h->extra, 0);

    std::string_view s(reinterpret_cast<const char*>(p.data()), p.size());
    if (s.find(contents) == 0) {
      count++;
    }
  }

  image.ignore_error();
  EXPECT_EQ(count, 1);
}

TEST_F(DevicetreeBootShimTest, DevicetreeItemWithAlias) {
  std::array<std::byte, 256> image_buffer;
  zbitl::Image<cpp20::span<std::byte>> image(image_buffer);

  ASSERT_TRUE(image.clear().is_ok());

  boot_shim::DevicetreeBootShim<DevicetreeItem1> shim("devicetree-boot-shim-test", fdt());
  auto match_result = shim.InitDevicetreeItems();
  ASSERT_TRUE(match_result.is_ok());
  // While we need aliases, in this case, the aliases are the first visited node,
  // so by the time we run into the node the matcher cares about, we already
  // resolved aliases.
  EXPECT_EQ(*match_result, 1);

  ASSERT_TRUE(shim.AppendItems(image).is_ok());

  CheckZbiHasItemWithContent(image, ZBI_TYPE_CMDLINE, "--visit-count=1");
}

TEST_F(DevicetreeBootShimTest, DevicetreeItemWithNoAlias) {
  std::array<std::byte, 256> image_buffer;
  zbitl::Image<cpp20::span<std::byte>> image(image_buffer);

  ASSERT_TRUE(image.clear().is_ok());

  boot_shim::DevicetreeBootShim<DevicetreeItem2> shim("devicetree-boot-shim-test", fdt());
  auto match_result = shim.InitDevicetreeItems();
  ASSERT_TRUE(match_result.is_ok());
  // While we need aliases, in this case, the aliases are the first visited node,
  // so by the time we run into the node the matcher cares about, we already
  // resolved aliases.
  EXPECT_EQ(*match_result, 2);

  ASSERT_TRUE(shim.AppendItems(image).is_ok());

  CheckZbiHasItemWithContent(image, ZBI_TYPE_CMDLINE, "--visit-count-b=2");
}

TEST_F(DevicetreeBootShimTest, MultipleDevicetreeItems) {
  std::array<std::byte, 256> image_buffer;
  zbitl::Image<cpp20::span<std::byte>> image(image_buffer);

  ASSERT_TRUE(image.clear().is_ok());

  boot_shim::DevicetreeBootShim<DevicetreeItem1, DevicetreeItem2> shim("devicetree-boot-shim-test",
                                                                       fdt());
  auto match_result = shim.InitDevicetreeItems();
  ASSERT_TRUE(match_result.is_ok());
  // While we need aliases, in this case, the aliases are the first visited node,
  // so by the time we run into the node the matcher cares about, we already
  // resolved aliases.
  EXPECT_EQ(*match_result, 2);

  ASSERT_TRUE(shim.AppendItems(image).is_ok());

  CheckZbiHasItemWithContent(image, ZBI_TYPE_CMDLINE, "--visit-count=1");
  CheckZbiHasItemWithContent(image, ZBI_TYPE_CMDLINE, "--visit-count-b=2");
}

}  // namespace
