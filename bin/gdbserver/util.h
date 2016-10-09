// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <cstdint>

#include <magenta/syscalls-types.h>
#include <magenta/types.h>

namespace debugserver {
namespace util {

// Decodes the 2 character ASCII string |hex| and returns the result in
// |out_byte|. Returns false if |hex| contains invalid characters.
bool DecodeByteString(const uint8_t hex[2], uint8_t* out_byte);

// Encodes |byte| into a 2 character ASCII string and returns the result in
// |out_hex|.
void EncodeByteString(uint8_t byte, uint8_t out_hex[2]);

// Logs the given |message| using the global errno variable, including the
// result of strerror in a nicely formatted way.
void LogErrorWithErrno(const std::string& message);

// Logs the given |message| using the string representation of |status| in a
// nicely formatted way.
void LogErrorWithMxStatus(const std::string& message, mx_status_t status);

// Potential Errno values used by GDB (see
// https://sourceware.org/gdb/onlinedocs/gdb/Errno-Values.html#Errno-Valuesfor
// reference). We don't rely on macros from errno.h because some of the integer
// definitions don't match.
enum class ErrorCode {
  PERM = 1,
  NOENT = 2,
  INTR = 4,
  BADF = 9,
  ACCES = 13,
  FAULT = 14,
  BUSY = 16,
  EXIST = 17,
  NODEV = 19,
  NOTDIR = 20,
  ISDIR = 21,
  INVAL = 22,
  NFILE = 23,
  MFILE = 24,
  FBIG = 27,
  NOSPC = 28,
  SPIPE = 29,
  ROFS = 30,
  NAMETOOLONG = 91,
  UNKNOWN = 9999
};

// Builds an error response packet based on |error_code|. For example, if
// |error_code| is EPERM then the return value is "E01".
std::string BuildErrorPacket(ErrorCode error_code);

// Parses a thread ID (and optionally a process ID). Returns true if the given
// expression is parsed successfully and returns the process and thread IDs in
// |out_pid| and |out_tid|. If a process ID is present, then the value of
// |out_has_pid| is set to true, and to false otherwise.
//
// Note that we are not using mx_koid_t here because it is defined as uint64_t
// and the GDB remote protocol allows a value of "-1" to refer to "all"
// processes/threads. So we do our best and use int64_t.
//
// Returns false if the values cannot be parsed or if they cannot be represented
// as an int64_t.
//
// (See
// https://sourceware.org/gdb/current/onlinedocs/gdb/Packets.html#thread%2did%20syntax
// for reference).
bool ParseThreadId(const uint8_t* bytes,
                   size_t num_bytes,
                   bool* out_has_pid,
                   int64_t* out_pid,
                   int64_t* out_tid);

// Verifies that the given command is formatted correctly and that the checksum
// is correct. Returns false verification fails. Otherwise returns true, and
// returns a pointer to the beginning of the packet data and the size of the
// packet data in the out parameters. A GDB Remote Protocol packet is defined
// as:
//
//   $<packet-data>#<2-digit checksum>
//
bool VerifyPacket(const uint8_t* packet,
                  size_t packet_size,
                  const uint8_t** out_packet_data,
                  size_t* out_packet_data_size);

// Extracts the prefix and the parameters from |packet| and returns them in the
// |out_*| variables. The prefix and the parameters should be separated by a
// colon (':'). If |packet| does not contain a colon, or if there are no
// characters following a colon, the returned parameters will be an empty
// string. |packet| cannot be empty.
//
// TODO(armansito): Consider passing around (const char*) instead of (const
// uint8_t*) since all the repeated casting is unnecessary. All GDB Remote
// Protocol packets bytes are ASCII encoded.
void ExtractParameters(const uint8_t* packet, size_t packet_size,
                       const uint8_t** out_prefix, size_t* prefix_size,
                       const uint8_t** out_params, size_t* params_size);

}  // namespace util
}  // namespace debugserver
