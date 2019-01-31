// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "generator.h"

#import <algorithm>
#import <iterator>

using std::ofstream;
using std::string;
using std::vector;

static const string in = "    ";
static const string inin = in + in;

static void write_syscall_signature_line(ofstream& os, const Syscall& sc, string name_prefix) {
    auto syscall_name = name_prefix + sc.name;

    os << "syscall_result " << syscall_name << "(";

    // Writes all arguments.
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        os << arg.as_cpp_declaration(false) << ", ";
    });

    os << "uint64_t pc) {\n";
}

bool KernelWrapperGenerator::header(ofstream& os) {
    if (!Generator::header(os))
        return false;

    os << "extern \"C\" {\n";
    return os.good();
}

bool KernelWrapperGenerator::syscall(ofstream& os, const Syscall& sc) {
    if (sc.is_vdso())
        return true;

    auto syscall_name = syscall_prefix_ + sc.name;
    write_syscall_signature_line(os, sc, wrapper_prefix_);
    os << in << "return do_syscall("
       << define_prefix_ << sc.name << ", "
       << "pc, "
       << "&VDso::ValidSyscallPC::" << sc.name << ", "
       << "[&](ProcessDispatcher* current_process) -> uint64_t {\n";

    string args;
    for (const TypeSpec& arg : sc.arg_spec) {
        if (!args.empty())
            args += ", ";
        if (arg.arr_spec) {
            args += "make_user_";
            args += arg.arr_spec->kind_lowercase_str();
            args += "_ptr(";
            args += arg.name;
            args += ")";
        } else {
            args += arg.name;
        }
    }

    vector<string> out_handles;

    sc.for_each_return([&](const TypeSpec& arg) {
        if (!args.empty())
            args += ", ";
        if (arg.arr_spec) {
            assert(arg.arr_spec->kind == ArraySpec::OUT);
            assert(arg.arr_spec->count == 1);
            if (arg.type == "zx_handle_t") {
                out_handles.push_back(arg.name);
                os << inin
                   << "user_out_handle out_handle_" << arg.name << ";\n";
                args += "&out_handle_";
                args += arg.name;
            } else {
                args += "make_user_out_ptr(";
                args += arg.name;
                args += ")";
            }
        } else {
            args += arg.name;
        }
    });

    os << inin
       << (sc.is_noreturn() ? "/*noreturn*/ " : "auto result = ")
       << syscall_name << "(" << args << ");\n";

    if (sc.is_noreturn()) {
        os << inin << "/* NOTREACHED */\n";
        os << inin << "return ZX_ERR_BAD_STATE;\n";
    } else {
        for (const auto& arg : out_handles) {
            os << inin << "if (out_handle_" << arg
               << ".begin_copyout(current_process, make_user_out_ptr("
               << arg << ")))\n"
               << inin << in << "return ZX_ERR_INVALID_ARGS;\n";
        }
        for (const auto& arg : out_handles) {
            os << inin << "out_handle_" << arg
               << ".finish_copyout(current_process);\n";
        }
        os << inin << "return result;\n";
    }

    os << in << "});\n"
       << "}\n";
    return os.good();
}

bool KernelWrapperGenerator::footer(ofstream& os) {
    os << "}\n";
    return os.good();
}
