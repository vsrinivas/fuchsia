// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace debugserver {

class Thread;

// Returns the register number for the "Program Counter Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetPCRegisterNumber();

// Returns the register number for the "Frame Pointer Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetFPRegisterNumber();

// Returns the register number for the "Stack Pointer Register" on the current
// platform. Returns -1, if this operation is not supported.
int GetSPRegisterNumber();

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

  // Loads and caches register values for |regset|.
  // Returns false if there is an error.
  virtual bool RefreshRegset(int regset) = 0;

  // Write the cached register set |regset| values back.
  // Returns false if there is an error.
  virtual bool WriteRegset(int regset) = 0;

  // Wrappers for general regs (regset0).
  bool RefreshGeneralRegisters();
  bool WriteGeneralRegisters();
  std::string GetGeneralRegistersAsString();
  bool SetGeneralRegistersFromString(const fxl::StringView& value);

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
  virtual std::string GetRegsetAsString(int regset) = 0;

  // Writes |value| to the cached value of |regset|.
  // |value| should be encoded the same way as the return value of
  // GetRegsetAsString(), as described above.
  // WriteRegset() must be called afterwards.
  // Returns true on success.
  virtual bool SetRegsetFromString(int regset,
                                   const fxl::StringView& value) = 0;

  // Gets the value of the register numbered |regno|. Returns an empty
  // string in case of an error or if |regno| is invalid. This avoids
  // making a syscall to refresh all register values and uses the most recently
  // cached values instead. Call RefreshRegisterValues() first to get the most
  // up-to-date values.
  virtual std::string GetRegisterAsString(int regno) = 0;

  // Get the value of register |regno| from the cached set
  // and store in |buffer|.
  // RefreshRegset() of the appropriate regset must be called first.
  // Returns a boolean indicating success.
  virtual bool GetRegister(int regno, void* buffer, size_t buf_size) = 0;

  // Sets the value of the register numbered |regno| to |value| of
  // size |value_size| bytes. Returns false if |regno| or
  // |value_size| are invalid on the current architecture.
  // WriteRegset() of the appropriate regset must be called afterwards.
  virtual bool SetRegister(int regno, const void* value, size_t value_size) = 0;

  // Get the value of the PC.
  // RefreshGeneralRegisters() must be called first.
  zx_vaddr_t GetPC();

  // Get the value of the SP.
  // RefreshGeneralRegisters() must be called first.
  zx_vaddr_t GetSP();

  // Get the value of the FP.
  // RefreshGeneralRegisters() must be called first.
  zx_vaddr_t GetFP();

  // Set the h/w singlestepping register.
  virtual bool SetSingleStep(bool enable) = 0;

  // Return a formatted display of |regset|.
  // RefreshRegset() of the appropriate regset must be called first.
  virtual std::string GetFormattedRegset(int regset) = 0;

  // Returns a string containing all 0s. This is used in our implementation to
  // return register values when there is a current inferior but no current
  // thread.
  // TODO(armansito): This is because we don't quite have a "stopped state" yet.
  // With the existing Zircon syscall surface, a process is either "started" or
  // "not started".
  static std::string GetUninitializedGeneralRegistersAsString();

  // Returns how many bytes each register value can hold. Returns 0 on
  // unsupported platforms.
  static size_t GetRegisterSize();

 protected:
  Registers(Thread* thread);

  Thread* thread() const { return thread_; }

  // Loads and caches register values for |regset|.
  // Returns false if there is an error.
  bool RefreshRegsetHelper(int regset, void* buf, size_t buf_size);

  // Write the cached register set |regset| values back.
  // Returns false if there is an error.
  bool WriteRegsetHelper(int regset, const void* buf, size_t buf_size);

  // Fill a regset buffer from its string representation.
  // This does not write the values to the cpu.
  // N.B. This helper assumes there is no padding in the regset buffer.
  bool SetRegsetFromStringHelper(int regset, void* buffer, size_t buf_size,
                                 const fxl::StringView& value);

 private:
  Thread* thread_;  // weak

  // Helper function for GetPC,GetSP,GetFP.
  zx_vaddr_t GetIntRegister(int regno);

  FXL_DISALLOW_COPY_AND_ASSIGN(Registers);
};

}  // namespace debugserver
