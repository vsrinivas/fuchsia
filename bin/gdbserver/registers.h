// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include <magenta/syscalls/exception.h>
#include <magenta/types.h>

#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace debugserver {

class Thread;

namespace arch {

// The x86-64 general register names.
enum class Amd64Register {
  RAX = 0,
  RBX,
  RCX,
  RDX,
  RSI,
  RDI,
  RBP,
  RSP,
  R8,
  R9,
  R10,
  R11,
  R12,
  R13,
  R14,
  R15,
  RIP,
  EFLAGS,
  NUM_REGISTERS
};

// Returns the register number for the "Program Counter Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetPCRegisterNumber();

// Returns the register number for the "Frame Pointer Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetFPRegisterNumber();

// Returns the register number for the "Stack Pointer Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetSPRegisterNumber();

// Maps the architecture-specific exception code to a UNIX compatible signal
// value that GDB understands. Returns -1  if the current architecture is not
// currently supported.
int ComputeGdbSignal(const mx_exception_context_t& exception_context);

// Registers represents an architecture-dependent general register set.
// This is an abstract, opaque interface that returns a register-value
// representation that complies with the GDB Remote Protocol with
// architecture-specific implementations.
class Registers {
 public:
  // Factory method for obtaining a Registers instance on the current
  // architecture for a particular thread |thread|.
  static std::unique_ptr<Registers> Create(Thread* thread);

  virtual ~Registers() = default;

  // Returns true if reading the general register set on the current
  // architecture is supported.
  virtual bool IsSupported() = 0;

  // Loads and caches register values. This is useful in conjunction with
  // GetRegisterValue to avoid unnecessary syscalls. Returns false if there is
  // an error.
  //
  // TODO(armansito): This is not really needed if we can read specific
  // registers using the mx_thread_read_state syscall with the correct hint.
  // Remove this once that is fully supported.
  virtual bool RefreshGeneralRegisters() = 0;

  // TODO(armansito): GetGeneralRegisters() and SetGeneralRegisters() below both
  // work with strings that conform to the GDB remote serial protocol. We should
  // change this so that this class is agnostic to the protocol and isolate such
  // parsing to the CommandHandler/Server. This way we can separate the back end
  // bits into a stand-alone library that we can use in gdb/lldb ports.

  // Returns a string containing sequentially encoded hexadecimal values of all
  // general registers. For example, on an architecture with 4 registers of 4
  // bytes each, this would return the following value:
  //
  //   WWWWWWWWXXXXXXXXYYYYYYYYZZZZZZZZ
  //
  // Returns an empty string if there is an error while reading the registers.
  virtual std::string GetGeneralRegisters() = 0;

  // Writes |value| to all general registers. |value| should be encoded the same
  // way as the return value of GetGeneralRegisters(), as described above.
  // Returns true on success.
  virtual bool SetGeneralRegisters(const ftl::StringView& value) = 0;

  // Gets the value of the register numbered |register_number|. Returns an empty
  // string in case of an error or if |register_number| is invalid. This avoids
  // making a syscall to refresh all register values and uses the most recently
  // cached values instead. Call RefreshRegisterValues() first to get the most
  // up-to-date values.
  virtual std::string GetRegisterValue(unsigned int register_number) = 0;

  // Sets the value of the register numbered |register_number| to |value| of
  // size |value_size| bytes. Returns false if |register_number| and
  // |value_size| are invalid on the current architecture.
  virtual bool SetRegisterValue(int register_number,
                                void* value,
                                size_t value_size) = 0;

  // Returns a string containing all 0s. This is used in our implementation to
  // return register values when there is a current inferior but no current
  // thread.
  // TODO(armansito): This is because we don't quite have a "stopped state" yet.
  // With the existing Magenta syscall surface, a process is either "started" or
  // "not started".
  static std::string GetUninitializedGeneralRegisters();

  // Returns how many bytes each register value can hold. Returns 0 on
  // unsupported platforms.
  static size_t GetRegisterSize();

 protected:
  Registers(Thread* thread);

  Thread* thread() const { return thread_; }

 private:
  Thread* thread_;  // weak

  FTL_DISALLOW_COPY_AND_ASSIGN(Registers);
};

}  // namespace arch
}  // namespace debugserver
