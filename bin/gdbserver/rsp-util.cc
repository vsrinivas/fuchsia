// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rsp-util.h"

#include <cctype>
#include <cinttypes>

#include <magenta/status.h>

#include "debugger-utils/util.h"
#include "debugger-utils/memory.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/ftl/strings/string_printf.h"

namespace debugserver {
namespace util {

std::string BuildErrorPacket(ErrorCode error_code) {
  std::string errstr =
      ftl::NumberToString<unsigned int>(static_cast<unsigned int>(error_code));
  if (errstr.length() == 1)
    errstr = "0" + errstr;
  return "E" + errstr;
}

bool ParseThreadId(const ftl::StringView& bytes,
                   bool* out_has_pid,
                   int64_t* out_pid,
                   int64_t* out_tid) {
  FTL_DCHECK(out_tid);
  FTL_DCHECK(out_has_pid);
  FTL_DCHECK(out_pid);

  if (bytes.empty())
    return false;

  if (bytes[0] != 'p') {
    *out_has_pid = false;
    return ftl::StringToNumberWithError<int64_t>(bytes, out_tid,
                                                 ftl::Base::k16);
  }

  *out_has_pid = true;

  // The pid and the tid are separated by a ".".
  size_t dot;
  bool found_dot = false;
  for (size_t i = 1; i < bytes.size(); i++) {
    if (bytes[i] != '.')
      continue;
    dot = i;
    found_dot = true;
    break;
  }

  // If there's no dot then tid is set to -1 (meaning all threads).
  if (!found_dot) {
    *out_tid = -1;
    return ftl::StringToNumberWithError<int64_t>(bytes.substr(1, dot - 1),
                                                 out_pid, ftl::Base::k16);
  }

  if (!ftl::StringToNumberWithError<int64_t>(bytes.substr(1, dot - 1), out_pid,
                                             ftl::Base::k16))
    return false;

  return ftl::StringToNumberWithError<int64_t>(bytes.substr(dot + 1), out_tid,
                                               ftl::Base::k16);
}

std::string EncodeThreadId(mx_koid_t pid, mx_koid_t tid) {
  std::string pid_string = ftl::NumberToString<mx_koid_t>(pid, ftl::Base::k16);
  std::string tid_string = ftl::NumberToString<mx_koid_t>(tid, ftl::Base::k16);

  return ftl::StringPrintf("p%s.%s", pid_string.c_str(), tid_string.c_str());
}

bool FindUnescapedChar(const char val,
                       const ftl::StringView& packet,
                       size_t* out_index) {
  FTL_DCHECK(out_index);

  size_t i;
  bool found = false;
  bool in_escape = false;
  for (i = 0; i < packet.size(); ++i) {
    // The previous character was the escape character. Exit the escape sequence
    // and continue.
    if (in_escape) {
      in_escape = false;
      continue;
    }

    if (packet[i] == kEscapeChar) {
      in_escape = true;
      continue;
    }

    if (packet[i] == val) {
      found = true;
      break;
    }
  }

  if (found)
    *out_index = i;

  return found;
}

// We take |packet| by copying since we modify it internally while processing
// it.
bool VerifyPacket(ftl::StringView packet, ftl::StringView* out_packet_data) {
  FTL_DCHECK(out_packet_data);

  if (packet.empty()) {
    FTL_LOG(ERROR) << "Empty packet";
    return false;
  }

  // Loop through the packet until we get to '$'. Ignore all other characters.
  // To quote the protocol specification "There are no notifications defined for
  // gdb to send at the moment", thus we ignore everything until the first '$'.
  // (see
  // https://sourceware.org/gdb/current/onlinedocs/gdb/Notification-Packets.html)
  size_t dollar_sign;
  if (!FindUnescapedChar('$', packet, &dollar_sign)) {
    FTL_LOG(ERROR) << "Packet does not start with \"$\": " << packet;
    return false;
  }

  packet.remove_prefix(dollar_sign);
  FTL_DCHECK(packet[0] == '$');

  // The packet should contain at least 4 bytes ($, #, 2-digit checksum).
  if (packet.size() < 4) {
    FTL_LOG(ERROR) << "Malformed packet: " << packet;
    return false;
  }

  size_t pound;
  if (!FindUnescapedChar('#', packet, &pound)) {
    FTL_LOG(ERROR) << "Packet does not contain \"#\"";
    return false;
  }

  ftl::StringView packet_data(packet.data() + 1, pound - 1);

  // Extract the packet checksum

  // First check if the packet contains the 2 digit checksum. The difference
  // between the payload size and the full packet size should exactly match the
  // number of required characters (i.e. '$', '#', and checksum).
  if (packet.size() - packet_data.size() != 4) {
    FTL_LOG(ERROR) << "Packet does not contain 2 digit checksum";
    return false;
  }

  // TODO(armansito): Ignore the checksum if we're in no-acknowledgment mode.

  uint8_t received_checksum;
  if (!util::DecodeByteString(packet.data() + pound + 1, &received_checksum)) {
    FTL_LOG(ERROR) << "Malformed packet checksum received";
    return false;
  }

  // Compute the checksum over packet payload
  uint8_t local_checksum = 0;
  for (char byte : packet_data)
    local_checksum += (uint8_t)byte;

  if (local_checksum != received_checksum) {
    FTL_LOG(ERROR) << "Bad checksum: computed = " << (unsigned)local_checksum
                   << ", received = " << (unsigned)received_checksum
                   << ", packet: " << packet;
    return false;
  }

  *out_packet_data = packet_data;

  return true;
}

void ExtractParameters(const ftl::StringView& packet,
                       ftl::StringView* out_prefix,
                       ftl::StringView* out_params) {
  FTL_DCHECK(!packet.empty());
  FTL_DCHECK(out_prefix);
  FTL_DCHECK(out_params);

  // Both query and set packets use can have parameters followed by a ':'
  // character.
  size_t colon;
  for (colon = 0; colon < packet.size(); colon++) {
    if (packet[colon] == ':')
      break;
  }

  // Everything up to |colon| is the prefix. If |colon| == |packet_size|,
  // then there are no parameters. If |colon| == (|packet_size| - 1) then
  // there is a ':' but no parameters following it.
  *out_prefix = packet.substr(0, colon);
  *out_params = packet.substr(
      colon + 1, packet.size() == colon ? 0 : packet.size() - colon - 1);
}

}  // namespace util
}  // namespace debugserver
