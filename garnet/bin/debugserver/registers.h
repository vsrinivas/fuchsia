// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUGSERVER_REGISTERS_H_
#define GARNET_BIN_DEBUGSERVER_REGISTERS_H_

#include <string>

#include <src/lib/fxl/strings/string_view.h>

namespace inferior_control {
class Thread;
}  // namespace inferior_control
using inferior_control::Thread;

namespace debugserver {

// Returns the register number for the "Program Counter Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetPCRegisterNumber();

// Returns the register number for the "Frame Pointer Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetFPRegisterNumber();

// Returns the register number for the "Stack Pointer Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetSPRegisterNumber();

// Returns a string containing all 0s. This is used in our implementation to
// return register values when there is a current inferior but no current
// thread.
// TODO(armansito): This is because we don't quite have a "stopped state" yet.
// With the existing Zircon syscall surface, a process is either "started" or
// "not started".
std::string GetUninitializedGeneralRegistersAsString();

std::string GetGeneralRegistersAsString(Thread* trhead);

// Fill a regset buffer.
// This does not write the values to the cpu.
// N.B. This helper assumes there is no padding in the regset buffer.
bool SetRegsetHelper(Thread* thread, int regset, const void* value,
                     size_t size);

bool SetGeneralRegistersFromString(Thread* thread,
                                   const fxl::StringView& value);

// TODO(armansito): The Get/Set AsString/FromString methods work with
// strings that conform to the GDB remote serial protocol. We should change
// this so that this class is agnostic to the protocol and isolate such
// parsing to the CommandHandler/Server. This way we can separate the back
// end bits into a stand-alone library that we can use in gdb/lldb ports.

// Returns a string containing sequentially encoded hexadecimal values of all
// registers in |regset|. For example, on an architecture with 4 registers of
// 4 bytes each, this would return the following value:
//
//   WWWWWWWWXXXXXXXXYYYYYYYYZZZZZZZZ
//
// RefreshRegset() must be called first.
// Returns an empty string if there is an error while reading the registers.
std::string GetRegsetAsString(Thread* thread, int regset);

// Writes |value| to the cached value of |regset|.
// |value| should be encoded the same way as the return value of
// GetRegsetAsString(), as described above.
// WriteRegset() must be called afterwards.
// Returns true on success.
bool SetRegsetFromString(Thread* thread, int regset,
                         const fxl::StringView& value);

// Gets the value of the register numbered |regno|. Returns an empty
// string in case of an error or if |regno| is invalid. This avoids
// making a syscall to refresh all register values and uses the most recently
// cached values instead. Call RefreshRegisterValues() first to get the most
// up-to-date values.
std::string GetRegisterAsString(Thread* thread, int regno);

}  // namespace debugserver

#endif  // GARNET_BIN_DEBUGSERVER_REGISTERS_H_
