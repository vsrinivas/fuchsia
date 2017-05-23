// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "kernel_invocation_generator.h"

using std::string;

static string invocation(std::ofstream& os, const string& out_var, const string& out_type,
                         const string& syscall_name, const Syscall& sc) {
    if (sc.is_noreturn()) {
        // no return - no need to set anything. the compiler
        // should know that we're never going anywhere from here
        os << syscall_name << "(";
        return ")";
    }

    os << out_var << " = ";

    // case 0: ret = static_cast<int64_t(sys_andy(
    os << "static_cast<" << out_type << ">(" << syscall_name << "(";
    return "))";
}

bool KernelInvocationGenerator::syscall(std::ofstream& os, const Syscall& sc) {
    if (sc.is_vdso())
        return true;

    string code_sp = string(8u, ' ');
    string block_sp = string(4u, ' ');
    string arg_sp = string(16u, ' ');

    auto syscall_name = syscall_prefix_ + sc.name;

    // case 0:
    os << block_sp << "case " << sc.index << ": {\n";

    os << code_sp << "CHECK_SYSCALL_PC(" << sc.name << ");\n";

    os << code_sp;

    // ret = static_cast<uint64_t>(syscall_whatevs(      )) -closer
    string close_invocation = invocation(os, return_var_, return_type_, syscall_name, sc);

    // Writes all arguments.
    int arg_index = 1;
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        os << "\n"
           << arg_sp
           << sc.maybe_wrap(arg.as_cpp_cast(arg_prefix_ + std::to_string(arg_index++)))
           << ",";
    });

    if (!os.good()) {
        return false;
    }

    if (sc.num_kernel_args() > 0) {
        // remove the comma.
        os.seekp(-1, std::ios_base::end);
    }

    os << close_invocation;

    if (sc.is_noreturn()) {
        os << "; // __noreturn__\n"
           << block_sp << "}\n";
    } else {
        os << ";\n";
        os << code_sp << "break;\n"
           << block_sp << "}\n";
    }

    return os.good();
}
