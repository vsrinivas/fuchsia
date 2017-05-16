// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "generator.h"

using std::ofstream;
using std::string;
using std::vector;

static const string in = "    ";
static const string inin = in + in;

static string invocation(ofstream& os, const string& out_type,
                         const string& syscall_name, const Syscall& sc) {
    if (sc.is_noreturn()) {
        // no return - no need to set anything. the compiler
        // should know that we're never going anywhere from here
        os << syscall_name << "(";
        return ")";
    }

    os << "return static_cast<" << out_type << ">(" << syscall_name << "(";
    return "))";
}

static void write_x86_syscall_signature_line(ofstream& os, const Syscall& sc, string name_prefix) {
    auto syscall_name = name_prefix + sc.name;

    os << "x86_64_syscall_result " << syscall_name << "(";

    // Writes all arguments.
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        os << arg.as_cpp_declaration(false) << ", ";
    });

    os << "uint64_t ip) {\n";
}

bool KernelWrapperGenerator::header(ofstream& os) {
    os << "extern \"C\" {\n";
    return os.good();
}

bool KernelWrapperGenerator::syscall(ofstream& os, const Syscall& sc) {
    // TODO(andymutton): Generate arm wrappers too.

    if (sc.is_vdso())
        return true;

    auto syscall_name = syscall_prefix_ + sc.name;
    os << "#if ARCH_X86_64\n";

    write_x86_syscall_signature_line(os, sc, wrapper_prefix_);

    os << in << "return do_syscall("
       << define_prefix_ << sc.name << ", "
       << "ip, "
       << "&VDso::ValidSyscallPC::" << sc.name << ", "
       << "[&]() {\n"
       << inin;

    string close_invocation = invocation(os, "uint64_t", syscall_name, sc);

    // Writes all arguments.
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        if (sc.is_no_wrap() || !arg.arr_spec) {
            os << arg.name << ", ";
        } else {
            os << "make_user_ptr(" << arg.name << "), ";
        }
    });

    if (sc.num_kernel_args() > 0) {
        // remove the comma space.
        os.seekp(-2, std::ios_base::end);
    }

    os << close_invocation;

    if (sc.is_noreturn()) {
        os << "; // __noreturn__\n";
        os << inin << "/* NOTREACHED */\n";
        os << inin << "return ERR_BAD_STATE;\n";
    } else {
        os << ";\n";
    }
    os << in << "});\n";
    os << "}\n";
    os << "#endif\n";
    return os.good();
}

bool KernelWrapperGenerator::footer(ofstream& os) {
    os << "}\n";
    return os.good();
}
