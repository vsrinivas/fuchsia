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
    virtual bool header(std::ofstream& os);
    virtual bool syscall(std::ofstream& os, const Syscall& sc) = 0;
    virtual bool footer(std::ofstream& os);

protected:
    virtual ~Generator() {}
};

// Interface for vDSO wrappers.
class CallWrapper {
public:
    virtual bool applies(const Syscall& sc) const = 0;
    virtual void preCall(std::ofstream& os, const Syscall& sc) const {}
    virtual void postCall(std::ofstream& os, const Syscall& sc, std::string return_var) const {}

protected:
    virtual ~CallWrapper() {}
};

// Generate the vDSO assembly stubs.
class VDsoAsmGenerator : public Generator {
public:
    VDsoAsmGenerator(const std::string& syscall_macro,
                     const std::string& name_prefix,
                     const std::vector<CallWrapper*>& call_wrappers)
        : syscall_macro_(syscall_macro),
          name_prefix_(name_prefix),
          wrappers_(call_wrappers) {}

    bool syscall(std::ofstream& os, const Syscall& sc) override;

private:
    const std::string syscall_macro_;
    const std::string name_prefix_;
    const std::vector<CallWrapper*> wrappers_;
};

// Generate the syscall number definitions.
class SyscallNumbersGenerator : public Generator {
public:
    SyscallNumbersGenerator(const std::string& define_prefix)
        : define_prefix_(define_prefix) {}

    bool syscall(std::ofstream& os, const Syscall& sc) override;
    bool footer(std::ofstream& os) override;

private:
    const std::string define_prefix_;
    int num_calls_ = 0;
};

// Generate debug trace info.
class TraceInfoGenerator : public Generator {
public:
    bool syscall(std::ofstream& os, const Syscall& sc) override;
};

// Generate category list.
class CategoryGenerator : public Generator {
public:
    bool syscall(std::ofstream& os, const Syscall& sc) override;
    bool footer(std::ofstream& os) override;

private:
    std::map<const std::string, std::vector<const std::string*>> category_map_;
};

/* Generates the kernel syscall jump table and accoutrements. */
class KernelBranchGenerator : public Generator {
public:
    bool header(std::ofstream& os) override;
    bool syscall(std::ofstream& os, const Syscall& sc) override;
};

/* Generates the kernel syscall wrappers. */
class KernelWrapperGenerator : public Generator {
public:
    KernelWrapperGenerator(const std::string& syscall_prefix, const std::string& wrapper_prefix,
                           const std::string& define_prefix)
        : syscall_prefix_(syscall_prefix), wrapper_prefix_(wrapper_prefix),
          define_prefix_(define_prefix) {}

    bool header(std::ofstream& os) override;
    bool syscall(std::ofstream& os, const Syscall& sc) override;
    bool footer(std::ofstream& os) override;

private:
    const std::string syscall_prefix_;
    const std::string wrapper_prefix_;
    const std::string define_prefix_;
};

/* Generates the Rust bindings. */
class RustBindingGenerator : public Generator {
public:
    bool header(std::ofstream& os) override;
    bool footer(std::ofstream& os) override;
    bool syscall(std::ofstream& os, const Syscall& sc) override;
};

// Writes the signature of a syscall, up to the end of the args list.
//
// Can wrap pointers with user_ptr.
// Can specify a type to substitute for no args.
// Doesn't write ';', '{}' or attributes.
void write_syscall_signature_line(std::ofstream& os, const Syscall& sc, std::string name_prefix,
                                  std::string before_args, std::string inter_arg,
                                  bool wrap_pointers_with_user_ptr, std::string no_args_type);

// Writes the return variable declaration for a syscall.
//
// Returns the name of the variable (or an empty string if the call was void).
std::string write_syscall_return_var(std::ofstream& os, const Syscall& sc);

// Writes an invocation of a syscall.
//
// Uses the argument names specified in the type description
// Performs no casting or pointer wrapping.
void write_syscall_invocation(std::ofstream& os, const Syscall& sc,
                              const std::string& return_var, const std::string& name_prefix);

void write_argument_annotation(std::ofstream& os, const TypeSpec& arg);
