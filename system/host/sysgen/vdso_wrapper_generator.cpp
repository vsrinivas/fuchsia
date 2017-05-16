// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vdso_wrapper_generator.h"

#include <algorithm>

using std::ofstream;
using std::string;
using std::vector;

static const string in = "    ";
static const string inin = in + in;

void write_extern_syscall_signature_line(ofstream& os, const Syscall& sc, string name_prefix) {
    os << "extern \"C\" {\nextern ";
    write_syscall_signature_line(
        os, sc, name_prefix, "", " ", false, "void");
    os << " __attribute__((visibility(\"hidden\")))";
    if (sc.is_noreturn()) {
        os << " __attribute__((__noreturn__))";
    }
    os << ";\n}\n";
}

void write_syscall_alias_line(ofstream& os, const Syscall& sc,
                              const string& name_prefix, const string& alias_prefix, const string& other_attributes) {

    os << "decltype(" << alias_prefix << sc.name << ") " << alias_prefix << sc.name
       << " __attribute__((";
    if (!other_attributes.empty()) {
        os << other_attributes << ", ";
    }
    os << "alias(\"" << name_prefix << sc.name << "\")));\n\n";
}

static bool none_apply(const Syscall& sc, const std::vector<CallWrapper*> wrappers) {
    for (const CallWrapper* wrapper : wrappers) {
        if (wrapper->applies(sc)) {
            return false;
        }
    }
    return true;
}

bool VdsoWrapperGenerator::syscall(ofstream& os, const Syscall& sc) {
    if (sc.is_vdso() || none_apply(sc, wrappers_)) {
        // Skip all calls implemented in the VDSO. They're on their own.
        return os.good();
    }

    // Declare the actual syscall as an extern - it is generated elsewhere.
    write_extern_syscall_signature_line(os, sc, call_prefix_);

    // Writing the wrapper.
    write_syscall_signature_line(os, sc, wrapper_prefix_, "", " ", false, "");
    os << " {\n"
       << in;
    std::string return_var = write_syscall_return_var(os, sc);
    pre_call(os, sc);
    os << in;
    // Invoke the actuall syscall.
    write_syscall_invocation(os, sc, return_var, call_prefix_);
    post_call(os, sc, return_var);

    if (!return_var.empty()) {
        os << in << "return " << return_var << ";\n";
    }
    os << "}\n\n";

    // Now alias the wrapper as the external and vdso symbols.
    write_syscall_alias_line(os, sc, wrapper_prefix_, external_prefix_, "weak");
    write_syscall_alias_line(os, sc, wrapper_prefix_, vdso_prefix_, "visibility(\"hidden\")");

    return os.good();
}

void VdsoWrapperGenerator::pre_call(ofstream& os, const Syscall& sc) const {
    std::for_each(wrappers_.begin(), wrappers_.end(), [&os, &sc](const CallWrapper* wrapper) {
        if (wrapper->applies(sc)) {
            wrapper->preCall(os, sc);
        }
    });
}

void VdsoWrapperGenerator::post_call(ofstream& os, const Syscall& sc, string return_var) const {
    std::for_each(wrappers_.rbegin(), wrappers_.rend(), [&os, &sc, &return_var](
                                                            const CallWrapper* wrapper) {
        if (wrapper->applies(sc)) {
            wrapper->postCall(os, sc, return_var);
        }
    });
}

bool TestWrapper::applies(const Syscall& sc) const {
    return sc.name == "syscall_test_wrapper";
}

void TestWrapper::preCall(ofstream& os, const Syscall& sc) const {
    os << in << "if (a < 0 || b < 0 || c < 0) return ERR_INVALID_ARGS;\n";
}

void TestWrapper::postCall(ofstream& os, const Syscall& sc, string return_var) const {
    os << in << "if (" << return_var << " > 50) return ERR_OUT_OF_RANGE;\n";
}

bool BlockingRetryWrapper::applies(const Syscall& sc) const {
    return sc.is_blocking();
}

void BlockingRetryWrapper::preCall(ofstream& os, const Syscall& sc) const {
    os << in << "do {\n";
}

void BlockingRetryWrapper::postCall(
    ofstream& os, const Syscall& sc, string return_var) const {
    os << in << "} while (unlikely(" << return_var << " == ERR_INTERRUPTED_RETRY));\n";
}
