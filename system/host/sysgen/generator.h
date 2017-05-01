// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ctime>
#include <fstream>
#include <string>

#include "types.h"

// Interface for syscall generators.
class Generator {
public:
  virtual bool header(std::ofstream& os) const;
  virtual bool syscall(std::ofstream& os, const Syscall& sc) const = 0;
  virtual bool footer(std::ofstream& os) const;
protected:
  virtual ~Generator() {}
};

// Generate the x86_64 userspace functions.
class X86AssemblyGenerator : public Generator {
public:
    X86AssemblyGenerator(const std::string& syscall_macro, const std::string& name_prefix) :
        syscall_macro_(syscall_macro),
        name_prefix_(name_prefix) {}

    bool syscall(std::ofstream& os, const Syscall& sc) const override;

private:
    const std::string syscall_macro_;
    const std::string name_prefix_;
};

// Generate the arm64 userspace functions.
class Arm64AssemblyGenerator : public Generator {
public:
    Arm64AssemblyGenerator(const std::string& syscall_macro, const std::string& name_prefix) :
        syscall_macro_(syscall_macro),
        name_prefix_(name_prefix) {}

    bool syscall(std::ofstream& os, const Syscall& sc) const override;

private:
    const std::string syscall_macro_;
    const std::string name_prefix_;
};

// Generate the syscall number definitions.
class SyscallNumbersGenerator : public Generator {
public:
    SyscallNumbersGenerator(const std::string& define_prefix) :
        define_prefix_(define_prefix) {}

    bool syscall(std::ofstream& os, const Syscall& sc) const override;

private:
    const std::string define_prefix_;
};

// Generate debug trace info.
class TraceInfoGenerator : public Generator {
public:
    bool syscall(std::ofstream& os, const Syscall& sc) const override;
};
