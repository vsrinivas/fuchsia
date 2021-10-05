/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

#include <algorithm>

#include <gtest/gtest.h>

namespace {

// Sanity test for log lines.
TEST(DebugTest, Logging) {
  BRCMF_INFO("foo %s", "foo");
  BRCMF_WARN("bar %s", "bar");
  BRCMF_ERR("baz %s", "baz");
}

TEST(DebugTest, ThrottleMacrosCompile) {
  BRCMF_ERR_THROTTLE("Throttled error message: %d", 42);
  BRCMF_WARN_THROTTLE("Throttled warning message: %s", "scary");
  BRCMF_INFO_THROTTLE("Throttled info message: %x", 0xf00);
  BRCMF_DBG_THROTTLE(WLANIF, "Throttled debug message: %c", '!');
}

}  // namespace
