// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/strings/string_view.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

namespace inferior_control {

class Thread;

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

  // Loads and caches register values for |regset|.
  // Returns false if there is an error.
  bool RefreshRegset(int regset);

  // Write the cached register set |regset| values back.
  // Returns false if there is an error.
  bool WriteRegset(int regset);

  // Wrappers for general regs (regset0).
  bool RefreshGeneralRegisters();
  bool WriteGeneralRegisters();

  // Fetch the general registers.
  // The returned pointer is valid until the thread is resumed or killed.
  // |RefreshGeneralRegisters()| must have already been called.
  zx_thread_state_general_regs_t* GetGeneralRegisters();

  // Get the value of register |regno| from the cached set
  // and store in |buffer|.
  // RefreshRegset() of the appropriate regset must be called first.
  // Returns a boolean indicating success.
  // TODO(dje): This method is deprecated.
  virtual bool GetRegister(int regno, void* buffer, size_t buf_size) = 0;

  // Sets the value of the register numbered |regno| to |value| of
  // size |value_size| bytes. Returns false if |regno| or
  // |value_size| are invalid on the current architecture.
  // WriteRegset() of the appropriate regset must be called afterwards.
  // TODO(dje): This method is deprecated.
  virtual bool SetRegister(int regno, const void* value, size_t value_size) = 0;

  // Accessors for frequently accessed registers.
  // RefreshGeneralRegisters() must be called first.
  virtual zx_vaddr_t GetPC() = 0;
  virtual zx_vaddr_t GetSP() = 0;
  virtual zx_vaddr_t GetFP() = 0;

  // Stepping over s/w breakpoint instructions requires setting PC.
  // RefreshGeneralRegisters() must be called first.
  virtual void SetPC(zx_vaddr_t pc) = 0;

  // Set the h/w singlestepping register.
  virtual bool SetSingleStep(bool enable) = 0;

  // Return a formatted display of |regset|.
  // RefreshRegset() of the appropriate regset must be called first.
  virtual std::string GetFormattedRegset(int regset) = 0;

 protected:
  Registers(Thread* thread);

  Thread* thread() const { return thread_; }

  // Loads and caches register values for |regset|.
  // Returns false if there is an error.
  bool RefreshRegsetHelper(int regset, void* buf, size_t buf_size);

  // Write the cached register set |regset| values back.
  // Returns false if there is an error.
  bool WriteRegsetHelper(int regset, const void* buf, size_t buf_size);

  // The general registers are generally always required, so allocate space
  // for them here.
  zx_thread_state_general_regs_t general_regs_ = {};

 private:
  Thread* thread_;  // non-owning

  FXL_DISALLOW_COPY_AND_ASSIGN(Registers);
};

}  // namespace inferior_control
