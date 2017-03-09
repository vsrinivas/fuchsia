// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <cctype>
#include <cinttypes>

#include <magenta/status.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/strings/string_printf.h"

#include "memory.h"

namespace debugserver {
namespace util {

namespace {

bool HexCharToByte(char hex_char, uint8_t* out_byte) {
  if (hex_char >= 'a' && hex_char <= 'f') {
    *out_byte = hex_char - 'a' + 10;
    return true;
  }

  if (hex_char >= '0' && hex_char <= '9') {
    *out_byte = hex_char - '0';
    return true;
  }

  if (hex_char >= 'A' && hex_char <= 'F') {
    *out_byte = hex_char - 'A' + 10;
    return true;
  }

  return false;
}

char HalfByteToHexChar(uint8_t byte) {
  FTL_DCHECK(byte < 0x10);

  if (byte < 10)
    return '0' + byte;

  return 'a' + (byte % 10);
}

}  // namespace

bool DecodeByteString(const char hex[2], uint8_t* out_byte) {
  FTL_DCHECK(out_byte);

  uint8_t msb, lsb;
  if (!HexCharToByte(hex[0], &msb) || !HexCharToByte(hex[1], &lsb))
    return false;

  *out_byte = lsb | (msb << 4);
  return true;
}

void EncodeByteString(const uint8_t byte, char out_hex[2]) {
  FTL_DCHECK(out_hex);

  out_hex[0] = HalfByteToHexChar(byte >> 4);
  out_hex[1] = HalfByteToHexChar(byte & 0x0f);
}

std::string EncodeByteArrayString(const uint8_t* bytes, size_t num_bytes) {
  const size_t kResultSize = num_bytes * 2;
  if (!kResultSize)
    return "";

  std::string result;
  result.resize(kResultSize);
  for (size_t i = 0; i < kResultSize; i += 2) {
    util::EncodeByteString(*bytes, const_cast<char*>(result.data() + i));
    ++bytes;
  }

  return result;
}

std::string EncodeString(const ftl::StringView& string) {
  auto bytes = reinterpret_cast<const uint8_t*>(string.data());
  return EncodeByteArrayString(bytes, string.size());
}

std::vector<uint8_t> DecodeByteArrayString(const ftl::StringView& string) {
  std::vector<uint8_t> result;
  if (string.size() % 2) {
    FTL_LOG(ERROR)
        << "Byte array string must have an even number of characters";
    return result;
  }

  if (string.empty())
    return result;

  const size_t kResultSize = string.size() / 2;
  result.resize(kResultSize);
  for (size_t i = 0; i < kResultSize; ++i) {
    if (!util::DecodeByteString(string.data() + (i * 2), result.data() + i))
      return std::vector<uint8_t>{};
  }

  return result;
}

std::string DecodeString(const ftl::StringView& string) {
  std::vector<uint8_t> charvec = DecodeByteArrayString(string);
  return std::string(charvec.begin(), charvec.end());
}

std::string EscapeNonPrintableString(const ftl::StringView& data) {
  std::string result;
  for (char c : data) {
    if (std::isprint(c)) {
      result.push_back(c);
      continue;
    }

    char str[4];
    str[0] = '\\';
    str[1] = 'x';
    EncodeByteString(static_cast<uint8_t>(c), str + 2);
    result.append(str, sizeof(str));
  }

  return result;
}

void LogError(const std::string& message) {
  FTL_LOG(ERROR) << message;
}

void LogErrorWithErrno(const std::string& message) {
  FTL_LOG(ERROR) << message << " (errno = " << errno << ", \""
                 << strerror(errno) << "\")";
}

void LogErrorWithMxStatus(const std::string& message, mx_status_t status) {
  FTL_LOG(ERROR) << message << ": " << mx_status_get_string(status)
                 << " (" << status << ")";
}

size_t JoinStrings(const std::deque<std::string>& strings,
                   const char delimiter,
                   char* buffer,
                   size_t buffer_size) {
  FTL_DCHECK(buffer);

  size_t index = 0, count = 0;
  for (const auto& str : strings) {
    FTL_DCHECK(index + str.length() <= buffer_size);
    memcpy(buffer + index, str.data(), str.length());
    index += str.length();
    if (++count == strings.size())
      break;
    FTL_DCHECK(index < buffer_size);
    buffer[index++] = delimiter;
  }

  return index;
}

const char* ExceptionName(mx_excp_type_t type) {
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (type) {
    CASE_TO_STR(MX_EXCP_GENERAL);
    CASE_TO_STR(MX_EXCP_FATAL_PAGE_FAULT);
    CASE_TO_STR(MX_EXCP_UNDEFINED_INSTRUCTION);
    CASE_TO_STR(MX_EXCP_SW_BREAKPOINT);
    CASE_TO_STR(MX_EXCP_HW_BREAKPOINT);
    CASE_TO_STR(MX_EXCP_THREAD_STARTING);
    CASE_TO_STR(MX_EXCP_THREAD_EXITING);
    CASE_TO_STR(MX_EXCP_GONE);
    default:
      return "UNKNOWN";
  }
#undef CASE_TO_STR
}

std::string ExceptionToString(mx_excp_type_t type,
                              const mx_exception_context_t& context) {
  std::string result(ExceptionName(type));
  // TODO(dje): Add more info to the string.
  return result;
}

bool ReadString(const Memory& m, mx_vaddr_t vaddr, char* ptr, size_t max) {
  while (max > 1) {
    if (!m.Read(vaddr, ptr, 1)) {
      *ptr = '\0';
      return false;
    }
    ptr++;
    vaddr++;
    max--;
  }
  *ptr = '\0';
  return true;
}

Argv BuildArgv(const ftl::StringView& args) {
  Argv result;

  // TODO: quoting, escapes, etc.
  // TODO: tweaks for gdb-like command simplification?
  // (e.g., p/x foo -> p /x foo)

  size_t n = args.size();
  for (size_t i = 0; i < n; ++i) {
    while (i < n && isspace(args[i]))
      ++i;
    if (i == n)
      break;
    size_t start = i;
    ++i;
    while (i < n && !isspace(args[i]))
      ++i;
    result.push_back(args.substr(start, i - start).ToString());
  }

  return result;
}

std::string ArgvToString(const Argv& argv) {
  if (argv.size() == 0)
    return "";

  std::string result(argv[0]);

  for (auto a = argv.begin() + 1; a != argv.end(); ++a)
    result += " " + *a;

  return result;
}

char* xstrdup(const char* s) {
  char* result = strdup(s);
  if (!result) {
    fprintf(stderr, "strdup OOM\n");
    exit(1);
  }
  return result;
}

const char* basename(const char* s) {
  // This implementation is copied from musl's basename.c,
  // but this will not modify its argument.
  size_t i;
  if (!s || !*s)
    return ".";
  i = strlen(s) - 1;
  if (i > 0 && s[i] == '/')
    return s;
  for (; i && s[i - 1] != '/'; i--)
    ;
  return s + i;
}

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

void hexdump_ex(FILE* out, const void* ptr, size_t len, uint64_t disp_addr) {
  uintptr_t address = (uintptr_t)ptr;
  size_t count;

  for (count = 0; count < len; count += 16) {
    union {
      uint32_t buf[4];
      uint8_t cbuf[16];
    } u;
    size_t s = ROUNDUP(MIN(len - count, 16), 4);
    size_t i;

    fprintf(out, ((disp_addr + len) > 0xFFFFFFFF)
            ? "0x%016" PRIx64 ": "
            : "0x%08" PRIx64 ": ",
            disp_addr + count);

    for (i = 0; i < s / 4; i++) {
      u.buf[i] = ((const uint32_t*)address)[i];
      fprintf(out, "%08x ", u.buf[i]);
    }
    for (; i < 4; i++) {
      fprintf(out, "         ");
    }
    fprintf(out, "|");

    for (i = 0; i < 16; i++) {
      char c = u.cbuf[i];
      if (i < s && isprint(c)) {
        fprintf(out, "%c", c);
      } else {
        fprintf(out, ".");
      }
    }
    fprintf(out, "|\n");
    address += 16;
  }
}

}  // namespace util
}  // namespace debugserver
