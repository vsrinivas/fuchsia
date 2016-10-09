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

bool VerifyPacket(const uint8_t* packet,
                  size_t packet_size,
                  const uint8_t** out_packet_data,
                  size_t* out_packet_data_size) {
  FTL_DCHECK(packet);
  FTL_DCHECK(out_packet_data);
  FTL_DCHECK(out_packet_data_size);

  // Loop through the packet until we get to '$'. Ignore all other characters.
  // TODO(armansito): Not all packets need to start with '$', e.g. notifications
  // and acknowledgments. Handle those here as well.
  for (; packet_size && *packet != '$'; packet++, packet_size--)
    ;

  // The packet should contain at least 4 bytes ($, #, 2-digit checksum).
  if (packet_size < 4)
    return false;

  if (packet[0] != '$') {
    FTL_LOG(ERROR) << "Packet does not start with \"$\": "
                   << std::string((const char*)packet, packet_size);
    return false;
  }

  const uint8_t* pound = nullptr;
  for (size_t i = 1; i < packet_size; ++i) {
    if (packet[i] == '#') {
      pound = packet + i;
      break;
    }
  }

  if (!pound) {
    FTL_LOG(ERROR) << "Packet does not contain \"#\"";
    return false;
  }

  const uint8_t* packet_data = packet + 1;
  size_t packet_data_size = pound - packet_data;

  // Extract the packet checksum

  // First check if the packet contains the 2 digit checksum. The difference
  // between the payload size and the full packet size should exactly match the
  // number of required characters (i.e. '$', '#', and checksum).
  if (packet_size - packet_data_size != 4) {
    FTL_LOG(ERROR) << "Packet does not contain 2 digit checksum";
    return false;
  }

  // TODO(armansito): Ignore the checksum if we're in no-acknowledgment mode.

  uint8_t received_checksum;
  if (!util::DecodeByteString(pound + 1, &received_checksum)) {
    FTL_LOG(ERROR) << "Malformed packet checksum received";
    return false;
  }

  // Compute the checksum over packet payload
  uint8_t local_checksum = 0;
  for (size_t i = 0; i < packet_data_size; ++i) {
    local_checksum += packet_data[i];
  }

  if (local_checksum != received_checksum) {
    FTL_LOG(ERROR) << "Bad checksum: computed = " << (unsigned)local_checksum
                   << ", received = " << (unsigned)received_checksum
                   << ", packet: "
                   << std::string((const char*)packet, packet_size);
    return false;
  }

  *out_packet_data = packet_data;
  *out_packet_data_size = packet_data_size;

  return true;
}

void ExtractParameters(const uint8_t* packet, size_t packet_size,
                       const uint8_t** out_prefix, size_t* out_prefix_size,
                       const uint8_t** out_params, size_t* out_params_size) {
  FTL_DCHECK(packet);
  FTL_DCHECK(packet_size);
  FTL_DCHECK(out_prefix);
  FTL_DCHECK(out_params);

  // Both query and set packets use can have parameters followed by a ':'
  // character.
  size_t colon;
  for (colon = 0; colon < packet_size; colon++) {
    if (packet[colon] == ':')
      break;
  }

  // Everything up to |colon| is the prefix. If |colon| == |packet_size|,
  // then there are no parameters. If |colon| == (|packet_size| - 1) then
  // there is a ':' but no parameters following it.
  *out_prefix = packet;
  *out_prefix_size = colon;
  *out_params = packet + colon + 1;
  *out_params_size = packet_size == colon ? 0 : packet_size - colon - 1;
}

}  // namespace util
}  // namespace debugserver
