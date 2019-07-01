/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with or without
 * fee is hereby granted, provided that the above copyright notice and this permission notice appear
 * in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "debug.h"

#include <algorithm>
#include <cctype>

namespace wlan {
namespace brcmfmac {

#if !defined(NDEBUG)

namespace {

// This is the default set of debugging messages that will be printed. Valid values are in
// enum class Debug::Filter.
//
// Include WLANIF messages in log output (at level INFO) to aid in recognizing important events.
// WLAN-1176: Remove WLANIF once things have stabilized.
constexpr uint32_t kBrcmfMsgFilter = static_cast<uint32_t>(Debug::Filter::kWLANIF);

}  // namespace

// static
bool Debug::IsFilterOn(Filter filter) {
  return (static_cast<uint32_t>(filter) & kBrcmfMsgFilter) != 0;
}

#endif  // !defined(NDEBUG)

// static
void Debug::PrintHexDump(uint32_t flag, const void* data, size_t length) {
  constexpr size_t kValuesPerLine = 64;
  constexpr size_t kMaxBytesDumpedCount = 4096;

  if (zxlog_level_enabled_etc(flag)) {
    driver_printf(flag, "%p:\n", data);

    const char* bytes = reinterpret_cast<const char*>(data);
    const size_t new_length = std::min<size_t>(length, kMaxBytesDumpedCount);
    for (size_t i = 0; i < new_length; i += kValuesPerLine) {
      char buffer[3 * kValuesPerLine];
      char* next = buffer;
      size_t line_width = std::min(kValuesPerLine, new_length - i);
      for (size_t j = 0; j < line_width; ++j) {
        next += sprintf(next, "%02x ", bytes[i + j]);
      }
      driver_printf(flag, "%04zx: %s\n", i, buffer);
    }
    if (length > kMaxBytesDumpedCount) {
      driver_printf(flag, "%04zx: ...\n", kMaxBytesDumpedCount);
    }
  }
}

// static
void Debug::PrintStringDump(uint32_t flag, const void* data, size_t length) {
  constexpr size_t kValuesPerLine = 64;
  constexpr size_t kMaxBytesDumpedCount = 256;

  if (zxlog_level_enabled_etc(flag)) {
    driver_printf(flag, "%p:\n", data);

    const char* bytes = reinterpret_cast<const char*>(data);
    const size_t new_length = std::min<size_t>(length, kMaxBytesDumpedCount);
    for (size_t i = 0; i < new_length; i += kValuesPerLine) {
      char buffer[kValuesPerLine];
      size_t line_width = std::min(kValuesPerLine, new_length - i);
      std::transform(bytes + i, bytes + i + line_width, buffer,
                     [](char c) { return std::isprint(c) ? c : '.'; });
      driver_printf(flag, "%04zx: %s\n", i, buffer);
    }
    if (length > kMaxBytesDumpedCount) {
      driver_printf(flag, "%04zx: ...\n", kMaxBytesDumpedCount);
    }
  }
}

// static
void Debug::CreateMemoryDump(const void* data, size_t length) {
  // No-op for now.
}

}  // namespace brcmfmac
}  // namespace wlan
