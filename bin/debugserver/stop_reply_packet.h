// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include <zircon/types.h>

#include "lib/fxl/strings/string_view.h"

namespace debugserver {

// Utility class for constructing Stop-Reply Packets as defined here:
// https://sourceware.org/gdb/current/onlinedocs/gdb/Stop-Reply-Packets.html
class StopReplyPacket final {
 public:
  // The type of stop-reply packet to be built. Packet parameters vary depending
  // on the type.
  enum class Type {
    // Program received a signal. This corresponds to a "T" or "S" packet.
    kReceivedSignal,

    // The process exited. Used when multiprocess protocol extensions are
    // supported. This corresponds to a "W" packet.
    kProcessExited,

    // The process terminated with a signal. Used when multiprocess protocol
    // extensions are supported. This corresponds to a "X" packet.
    kProcessTerminatedWithSignal,

    // A thread exited. Corresponds to a "w" packet and is used with the
    // QThreadEvents packet.
    kThreadExited,
  };

  explicit StopReplyPacket(Type type);
  ~StopReplyPacket() = default;

  // Sets the signal number. Depending on the packet type this either represents
  // a signal number received from the OS or an exit status in the type is equal
  // to kProcessExited or kThreadExited. Can only be used if the packet type
  // contains a signal number or exit status.
  void SetSignalNumber(uint8_t signal_number);

  // Sets the thread and process IDs to be reported. This can only be set if the
  // packet type is equal to kReceivedSignal.
  void SetThreadId(zx_koid_t process_id, zx_koid_t thread_id);

  // Adds a register value to be reported. This can only be set if the packet
  // type is equal to kReceivedSignal. |value| must contain a series of bytes in
  // target byte order, with each byte represent by a two digit ASCII hex
  // number.
  void AddRegisterValue(uint8_t register_number, const fxl::StringView& value);

  // Sets a stop reason. This can only be set if the packet type is equal to
  // kReceivedSignal. Setting a stop-reason overrides any previously set signal
  // number in favor of "05", the trap signal.
  void SetStopReason(const fxl::StringView& reason);

  // Returns the encoded packet payload. Returns an empty string if the minimum
  // required parameters have not been set for the type this object was
  // initialized with.
  std::vector<char> Build() const;

 private:
  StopReplyPacket() = default;

  // Returns true if any parameters have been set.
  bool HasParameters() const;

  Type type_;
  uint8_t signo_;
  std::string tid_string_;
  std::vector<std::vector<char>> register_values_;
  std::string stop_reason_;
};

}  // namespace debugserver
