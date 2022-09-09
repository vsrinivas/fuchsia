// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEVICETREE_TESTS_TEST_HELPER_H_
#define ZIRCON_KERNEL_LIB_DEVICETREE_TESTS_TEST_HELPER_H_

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <filesystem>
#include <string_view>

// Sets |path| to the absolute path to a test file |filename|.
void GetTestDataPath(std::string_view filename, std::filesystem::path& path);

// Reads the contents of |filename| into |buffer|.
void ReadTestData(std::string_view filename, cpp20::span<uint8_t> buffer);

#endif  // ZIRCON_KERNEL_LIB_DEVICETREE_TESTS_TEST_HELPER_H_
