// Copyright (c) 2019 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/firmware.h"

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <string>

#include <zxtest/zxtest.h>

namespace wlan {
namespace brcmfmac {
namespace {

// This is a simple test to verify the NVRAM parsing functionality.
TEST(FirmwareTest, ParseNvram) {
  struct InputAndResult {
    std::string input;
    std::string expected_result;
  };

  using namespace std::string_literals;
  InputAndResult inputs_and_results[] = {
      // If there is no "boardrev=" key, ensure the default is applied.
      {
          " # comment\n\n"s,
          "boardrev=0xff\0\0\0\x04\x00\xFB\xFF"s,
      },
      // Multiple and duplicate keys.
      {
          "foo=1\nbar=2\nbaz=3\nfoo=4\nboardrev=0x0\n"s,
          "foo=1\0bar=2\0baz=3\0foo=4\0boardrev=0x0\0\0\0\0\x0A\x00\xF5\xFF"s,
      },
      // Mixture of whitespaces and comments.
      {
          "\t#comment\n\n\t foo = bar   baz\nboardrev = 0xcafe  # foo \n"s,
          "foo=bar   baz\0boardrev=0xcafe\0\0\0\x08\x00\xF7\xFF"s,
      },
      // DOS newlines, because those are a thing.
      {
          "#comment1\n#comment2\r\n\r\nfoo = bar \r\nboardrev = 0xcafe  # foo \r\n"s,
          "foo=bar\0boardrev=0xcafe\0\0\0\0\0\x07\x00\xF8\xFF"s,
      },
      // Comments and ignored "special keys".
      {
          " # comment\n\nRAW1=1\ndevpath_foo=foo\npcie/bar=bar\n"s,
          "boardrev=0xff\0\0\0\x04\x00\xFB\xFF"s,
      },
  };

  for (const auto& input_and_result : inputs_and_results) {
    const std::string& input = input_and_result.input;
    const std::string& expected_result = input_and_result.expected_result;
    std::string result;
    zx_status_t status = ParseNvramBinary(input, &result);
    EXPECT_EQ(ZX_OK, status, "status=%s", zx_status_get_string(status));
    EXPECT_EQ(expected_result, result, "input=\"%s\"", input.c_str());
  }
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
