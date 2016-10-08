// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <runtime/status.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

namespace debugserver {
namespace util {
namespace {

bool HexCharToByte(uint8_t hex_char, uint8_t* out_byte) {
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

bool DecodeByteString(const uint8_t hex[2], uint8_t* out_byte) {
  FTL_DCHECK(out_byte);

  uint8_t msb, lsb;
  if (!HexCharToByte(hex[0], &msb) || !HexCharToByte(hex[1], &lsb))
    return false;

  *out_byte = lsb | (msb << 4);
  return true;
}

void EncodeByteString(uint8_t byte, uint8_t out_hex[2]) {
  FTL_DCHECK(out_hex);

  out_hex[0] = HalfByteToHexChar(byte >> 4);
  out_hex[1] = HalfByteToHexChar(byte & 0x0f);
}

void LogErrorWithErrno(const std::string& message) {
  FTL_LOG(ERROR) << message << " (errno = " << errno << ", \""
                 << strerror(errno) << "\")";
}

void LogErrorWithMxStatus(const std::string& message, mx_status_t status) {
  FTL_LOG(ERROR) << message << ": " << mx_strstatus(status) << " (" << status
                 << ")";
}

std::string BuildErrorPacket(ErrorCode error_code) {
  std::string errstr =
      ftl::NumberToString<unsigned int>(static_cast<unsigned int>(error_code));
  if (errstr.length() == 1)
    errstr = "0" + errstr;
  return "E" + errstr;
}

bool ParseThreadId(const uint8_t* bytes,
                   size_t num_bytes,
                   bool* out_has_pid,
                   int64_t* out_pid,
                   int64_t* out_tid) {
  FTL_DCHECK(bytes);
  FTL_DCHECK(out_tid);
  FTL_DCHECK(out_has_pid);
  FTL_DCHECK(out_pid);

  if (num_bytes == 0)
    return false;

  if (bytes[0] != 'p') {
    *out_has_pid = false;
    return ftl::StringToNumberWithError<int64_t>(
        ftl::StringView((const char*)bytes, num_bytes), out_tid);
  }

  *out_has_pid = true;

  // The pid and the tid are separated by a ".".
  size_t dot;
  bool found_dot = false;
  for (size_t i = 1; i < num_bytes; i++) {
    if (bytes[i] != '.')
      continue;
    dot = i;
    found_dot = true;
    break;
  }

  // Didn't find a dot.
  if (!found_dot)
    return false;

  if (!ftl::StringToNumberWithError<int64_t>(
          ftl::StringView((const char*)bytes + 1, dot - 1), out_pid)) {
    FTL_LOG(ERROR) << "Could not parse process id: "
                   << std::string((const char*)bytes, num_bytes);
    return false;
  }

  return ftl::StringToNumberWithError<int64_t>(
      ftl::StringView((const char*)bytes + dot + 1, num_bytes - dot - 1),
      out_tid);
}

}  // namespace util
}  // namespace debugserver
