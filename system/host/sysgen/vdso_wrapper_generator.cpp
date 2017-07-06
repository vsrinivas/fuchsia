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

    // Writing the wrapper.
    write_syscall_signature_line(os, sc, wrapper_prefix_, "", " ", false, "");
    os << " {\n"
       << in;
    std::string return_var = write_syscall_return_var(os, sc);
    pre_call(os, sc);
    os << inin;
    // Invoke the actuall syscall.
    write_syscall_invocation(os, sc, return_var, call_prefix_);
    post_call(os, sc, return_var);

    if (!return_var.empty()) {
        os << in << "return " << return_var << ";\n";
    }
    os << "}\n\n";

    // Now put the wrapper into the public interface.
    os << "VDSO_INTERFACE_FUNCTION(mx_" << sc.name << ");\n\n";

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
    os << in << "if (a < 0 || b < 0 || c < 0) return MX_ERR_INVALID_ARGS;\n";
}

void TestWrapper::postCall(ofstream& os, const Syscall& sc, string return_var) const {
    os << in << "if (" << return_var << " > 50) return MX_ERR_OUT_OF_RANGE;\n";
}

bool BlockingRetryWrapper::applies(const Syscall& sc) const {
    return sc.is_blocking();
}

void BlockingRetryWrapper::preCall(ofstream& os, const Syscall& sc) const {
    os << in << "do {\n";
}

void BlockingRetryWrapper::postCall(
    ofstream& os, const Syscall& sc, string return_var) const {
    os << in << "} while (unlikely(" << return_var << " == MX_ERR_INTERNAL_INTR_RETRY));\n";
}
