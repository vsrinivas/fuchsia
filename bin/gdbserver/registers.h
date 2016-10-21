// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include <magenta/types.h>

#include "lib/ftl/macros.h"

namespace debugserver {
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

// Registers represents an architecture-dependent general register set.
// This is an abstract, opaque interface that returns a register-value
// representation that complies with the GDB Remote Protocol with
// architecture-specific implementations.
class Registers {
 public:
  // Factory method for obtaining a Registers instance on the current
  // architecture for a particular thread with handle |thread_handle|.
  static std::unique_ptr<Registers> Create(const mx_handle_t thread_handle);

  virtual ~Registers() = default;

  // Returns true if reading the general register set on the current
  // architecture is supported.
  virtual bool IsSupported() = 0;

  // Returns a string containing sequentially encoded hexadecimal values of all
  // general registers. For example, on an architecture with 4 registers of 4
  // bytes each, this would return the following value:
  //
  //   WWWWWWWWXXXXXXXXYYYYYYYYZZZZZZZZ
  //
  // Returns an empty string if there is an error while reading the registers.
  virtual std::string GetGeneralRegisters() = 0;

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

 protected:
  Registers(const mx_handle_t thread_handle);

  mx_handle_t thread_handle() const { return thread_handle_; }

 private:
  mx_handle_t thread_handle_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Registers);
};

}  // namespace arch
}  // namespace debugserver
