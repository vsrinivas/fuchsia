// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <ctime>

#include "generator.h"
#include "header_generator.h"
#include "kernel_invocation_generator.h"
#include "vdso_wrapper_generator.h"

#include "sysgen_generator.h"

using std::string;
using std::map;
using std::vector;

const map<string, string> user_attrs = {
    {"noreturn", "__NO_RETURN"},
    {"const", "__CONST"},
    {"deprecated", "__DEPRECATED"},

    // All vDSO calls are "leaf" in the sense of the GCC attribute.
    // It just means they can't ever call back into their callers'
    // own translation unit.  No vDSO calls make callbacks at all.
    {"*", "__LEAF_FN"},
};

const map<string, string> kernel_attrs = {
    {"noreturn", "__NO_RETURN"},
};

static TestWrapper test_wrapper;
static BlockingRetryWrapper blocking_wrapper;
static vector<CallWrapper*> wrappers = {&test_wrapper, &blocking_wrapper};

static VdsoWrapperGenerator vdso_wrapper_generator(
    "_mx_",        // wrapper function name
    "SYSCALL_mx_", // syscall implementation name
    wrappers);

static KernelBranchGenerator kernel_branch;

static KernelInvocationGenerator kernel_code(
    "sys_",     // function prefix
    "ret",      // variable to assign invocation result to
    "uint64_t", // type of result variable
    "arg");     // prefix for syscall arguments);

static KernelWrapperGenerator kernel_wrappers(
    "sys_",     // function prefix
    "wrapper_", // wrapper prefix
    "MX_SYS_"); // syscall numbers constant prefix

static bool skip_nothing(const Syscall&) {
    return false;
}

static bool skip_internal(const Syscall& sc) {
    return sc.is_internal();
}

static bool skip_vdso(const Syscall& sc) {
    return sc.is_vdso();
}

static HeaderGenerator user_header(
    "extern ",                       // function prefix
    {
        {"mx_", skip_internal},
        {"_mx_", skip_internal},
    },
    "void",                          // no-args special type
    false,                           // wrap pointers
    user_attrs);

static HeaderGenerator vdso_header(
    "__LOCAL extern ", // function prefix
    {
        {"VDSO_mx_", skip_nothing},
        {"SYSCALL_mx_", skip_vdso},
    },
    "void",                                            // no-args special type
    false,
    user_attrs);

static HeaderGenerator kernel_header(
    "",
    {
        {"sys_", skip_vdso},
    },
    "",
    true,
    kernel_attrs);

static VDsoAsmGenerator vdso_asm_generator(
    "m_syscall", // syscall macro name
    "mx_",       // syscall name prefix
    wrappers);

static SyscallNumbersGenerator syscall_num_generator("#define MX_SYS_");

static RustBindingGenerator rust_binding_generator;
static TraceInfoGenerator trace_generator;
static CategoryGenerator category_generator;

const map<string, Generator&> type_to_generator = {
    // The user header, pure C.
    {"user-header", user_header},

    // The vDSO-internal header, pure C.  (VDsoHeaderC)
    {"vdso-header", vdso_header},

    // The kernel header, C++.
    {"kernel-header", kernel_header},

    // The kernel C++ code. A switch statement set.
    {"kernel-code", kernel_code},

    // The kernel assembly branches and jump table.
    {"kernel-branch", kernel_branch},

    // The kernel C++ wrappers.
    {"kernel-wrappers", kernel_wrappers},

    //  The assembly file for x86-64.
    {"x86-asm", vdso_asm_generator},

    //  The assembly include file for ARM64.
    {"arm-asm", vdso_asm_generator},

    // A C header defining MX_SYS_* syscall number macros.
    {"numbers", syscall_num_generator},

    // The trace subsystem data, to be interpreted as an array of structs.
    {"trace", trace_generator},

    // Rust bindings.
    {"rust", rust_binding_generator},

    // vDSO wrappers for additional behaviour in user space.
    {"vdso-wrappers", vdso_wrapper_generator},

    // Category list.
    {"category", category_generator},
};

const map<string, string> type_to_default_suffix = {
    {"user-header", ".user.h"},
    {"vdso-header", ".vdso.h"},
    {"kernel-header", ".kernel.h"},
    {"kernel-branch", ".kernel-branch.S"},
    {"kernel-code", ".kernel.inc"},
    {"kernel-wrappers", ".kernel-wrappers.inc"},
    {"x86-asm", ".x86-64.S"},
    {"arm-asm", ".arm64.S"},
    {"numbers", ".syscall-numbers.h"},
    {"trace", ".trace.inc"},
    {"rust", ".rs"},
    {"vdso-wrappers", ".vdso-wrappers.inc"},
    {"category", ".category.inc"},
};

const map<string, string>& get_type_to_default_suffix() {
    return type_to_default_suffix;
}

const map<string, Generator&>& get_type_to_generator() {
    return type_to_generator;
}

bool SysgenGenerator::AddSyscall(Syscall& syscall) {
    if (!syscall.validate())
        return false;
    syscall.assign_index(&next_index_);
    calls_.push_back(syscall);
    return true;
}

bool SysgenGenerator::Generate(const map<string, string>& type_to_filename) {
    for (auto& entry : type_to_filename) {
        if (!generate_one(entry.second, type_to_generator.at(entry.first), entry.first))
            return false;
    }
    return true;
}

bool SysgenGenerator::verbose() const {
    return verbose_;
}

bool SysgenGenerator::generate_one(
    const string& output_file, Generator& generator, const string& type) {
    std::ofstream ofile;
    ofile.open(output_file.c_str(), std::ofstream::out);

    if (!generator.header(ofile)) {
        print_error("i/o error", output_file);
        return false;
    }

    if (!std::all_of(calls_.begin(), calls_.end(),
                     [&generator, &ofile](const Syscall& sc) {
                         return generator.syscall(ofile, sc);
                     })) {
        print_error("generation failed", output_file);
        return false;
    }

    if (!generator.footer(ofile)) {
        print_error("i/o error", output_file);
        return false;
    }

    return true;
}

void SysgenGenerator::print_error(const char* what, const string& file) {
    fprintf(stderr, "error: %s for %s\n", what, file.c_str());
}
