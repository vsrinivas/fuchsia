// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_TEST_HELPER_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_TEST_HELPER_H_

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace boot_shim::testing {

struct BufferOwner {
  static BufferOwner New(size_t);

  cpp20::span<std::byte> buffer;
  std::unique_ptr<std::byte[]> owner;
};

class TestHelper {
 public:
  TestHelper();

  // Cannot be copied or moved.
  TestHelper(const TestHelper&) = delete;
  TestHelper(TestHelper&&) = delete;
  TestHelper& operator=(const TestHelper&) = delete;
  TestHelper& operator=(TestHelper&&) = delete;

  ~TestHelper();

  FILE* log() { return log_; }

  std::vector<std::string> LogLines();

  void ExpectLogLines(std::initializer_list<std::string_view> expected);

  BufferOwner GetZbiBuffer(size_t size = 4096);

 private:
  [[maybe_unused]] char* log_buffer_ = nullptr;
  [[maybe_unused]] size_t log_buffer_size_ = 0;
  // Note this must be declared last so the previous two are initialized first.
  FILE* log_ = nullptr;
};

inline cpp20::span<const std::byte> Payload(std::string_view str) {
  return {reinterpret_cast<const std::byte*>(str.data()), str.size()};
}

template <typename T>
inline cpp20::span<const std::byte> Payload(cpp20::span<T> data) {
  return cpp20::as_bytes(data);
}

inline std::string_view StringPayload(cpp20::span<std::byte> payload) {
  return {reinterpret_cast<const char*>(payload.data()), payload.size()};
}

}  // namespace boot_shim::testing

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_TEST_HELPER_H_
