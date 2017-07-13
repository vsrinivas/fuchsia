// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctime>
#include <fstream>

#include "generator.h"

using std::ofstream;
using std::string;

static constexpr char kAuthors[] = "The Fuchsia Authors";
static constexpr char kWrapMacro[] = "MX_SYSCALL_PARAM_ATTR";
static constexpr char kDefaultHandleAnnotation[] = "handle_use";
bool is_identifier_keyword(const string& iden);

bool Generator::header(ofstream& os) {
    auto t = std::time(nullptr);
    auto ltime = std::localtime(&t);

    os << "// Copyright " << ltime->tm_year + 1900
       << " " << kAuthors << ". All rights reserved.\n";
    os << "// This is a GENERATED file, see //magenta/system/host/sysgen.\n";
    os << "// The license governing this file can be found in the LICENSE file.\n\n";

    return os.good();
}

bool Generator::footer(ofstream& os) {
    os << "\n";
    return os.good();
}

bool VDsoAsmGenerator::syscall(ofstream& os, const Syscall& sc) {
    if (!sc.is_vdso()) {
        bool is_public = !sc.is_internal();
        for (const CallWrapper* wrapper : wrappers_) {
            if (wrapper->applies(sc)) {
                is_public = false;
                break;
            }
        }

        // m_syscall name, syscall_num, nargs
        os << syscall_macro_
           << " " << name_prefix_ << sc.name
           << " " << sc.index
           << " " << sc.num_kernel_args()
           << " " << (is_public ? 1 : 0)
           << "\n";
    }
    return os.good();
}

bool KernelBranchGenerator::header(ofstream& os) {
    if (!Generator::header(os))
        return false;

    os << "start_syscall_dispatch\n";
    return os.good();
}

bool KernelBranchGenerator::syscall(ofstream& os, const Syscall& sc) {
    if (sc.is_vdso()) {
        return os.good();
    }
    os << "syscall_dispatch " << sc.num_kernel_args() << " " << sc.name << "\n";
    return os.good();
}

bool SyscallNumbersGenerator::syscall(ofstream& os, const Syscall& sc) {
    if (sc.is_vdso())
        return true;

    num_calls_++;
    os << define_prefix_ << sc.name << " " << sc.index << "\n";
    return os.good();
}

bool SyscallNumbersGenerator::footer(ofstream& os) {
    os << define_prefix_ << "COUNT " << num_calls_ << "\n";
    return os.good();
}

bool TraceInfoGenerator::syscall(ofstream& os, const Syscall& sc) {
    if (sc.is_vdso())
        return true;

    // Can be injected as an array of structs or into a tuple-like C++ container.
    os << "{" << sc.index << ", " << sc.num_kernel_args() << ", "
       << '"' << sc.name << "\"},\n";

    return os.good();
}

bool CategoryGenerator::syscall(ofstream& os, const Syscall& sc) {
    for (const auto& attr : sc.attributes) {
        if (attr != "*" && attr != "internal")
            category_map_[attr].push_back(&sc.name);
    }
    return true;
}

bool CategoryGenerator::footer(ofstream& os) {
    for (const auto& category : category_map_) {
        os << "\n#define HAVE_SYSCALL_CATEGORY_" << category.first << " 1\n";
        os << "SYSCALL_CATEGORY_BEGIN(" << category.first << ")\n";

        for (auto sc : category.second)
            os << "    SYSCALL_IN_CATEGORY(" << *sc << ")\n";

        os << "SYSCALL_CATEGORY_END(" << category.first << ")\n";
    }
    return os.good();
}

void write_syscall_signature_line(ofstream& os, const Syscall& sc, string name_prefix,
                                  string before_args, string inter_arg,
                                  bool wrap_pointers_with_user_ptr, string no_args_type) {
    auto syscall_name = name_prefix + sc.name;

    // writes "[return-type] prefix_[syscall-name]("
    os << sc.return_type() << " " << syscall_name << "(";

    // Writes all arguments.
    os << before_args;
    bool first = true;
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        if (!first) {
            os << inter_arg;
        }
        first = false;
        write_argument_annotation(os, arg);
        os << arg.as_cpp_declaration(wrap_pointers_with_user_ptr) << ",";
    });

    if (sc.num_kernel_args() > 0) {
        // remove the comma.
        os.seekp(-1, std::ios_base::end);
    } else {
        os << no_args_type;
    }

    os << ")";
}

string write_syscall_return_var(ofstream& os, const Syscall& sc) {
    string return_var = sc.is_void_return() ? "" : "ret";
    if (!sc.is_void_return()) {
        os << sc.return_type() << " " << return_var << ";\n";
    }
    return return_var;
}

void write_syscall_invocation(ofstream& os, const Syscall& sc,
                              const string& return_var, const string& name_prefix) {
    if (!return_var.empty()) {
        os << return_var << " = ";
    }

    os << name_prefix << sc.name << "(";

    // Writes all arguments.
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        os << arg.name << ", ";
    });

    if (sc.num_kernel_args() > 0) {
        // remove the comma and space.
        os.seekp(-2, std::ios_base::end);
    }

    os << ");\n";
}

void write_argument_annotation(std::ofstream& os, const TypeSpec& arg) {
    bool has_annotation = false;
    for (const auto& a : arg.attributes) {
        if (!a.empty() && !is_identifier_keyword(a)) {
            has_annotation = true;
            os << kWrapMacro << "(" << a << ") ";
        }
    }
    // If arg type is a handle (not an array) and no annotation is present, use default annotation
    if (!has_annotation && arg.type == "mx_handle_t" && arg.arr_spec == nullptr) {
        os << kWrapMacro << "(" << kDefaultHandleAnnotation << ") ";
    }
}
