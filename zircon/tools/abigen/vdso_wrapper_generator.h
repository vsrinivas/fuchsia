// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "generator.h"

// Generates wrappers in the vDSO that add behavior defined by the given
// CallWrapper list.
class VdsoWrapperGenerator : public Generator {
public:
    VdsoWrapperGenerator(std::string wrapper_prefix,
                         std::string call_prefix,
                         std::vector<CallWrapper*> call_wrappers)
        : wrapper_prefix_(wrapper_prefix),
          call_prefix_(call_prefix), wrappers_(call_wrappers) {}

    bool syscall(std::ofstream& os, const Syscall& sc) override;

private:
    void pre_call(std::ofstream& os, const Syscall& sc) const;
    void post_call(std::ofstream& os, const Syscall& sc, std::string return_var) const;

    std::string wrapper_prefix_;
    std::string call_prefix_;
    std::vector<CallWrapper*> wrappers_;
};

// Wrapper for testing that wrappers work correctly. Applied only to syscall_test_wrapper.
class TestWrapper : public CallWrapper {
public:
    bool applies(const Syscall& sc) const override;
    // Adds a precondition that all args are > 0;
    void preCall(std::ofstream& os, const Syscall& sc) const override;
    // Adds a postcondition that the result is < 50;
    void postCall(std::ofstream& os, const Syscall& sc, std::string return_var) const override;
};

// Wraps a syscall with the "blocking" attribute with code that will
// automatically retry if interrupted.
class BlockingRetryWrapper : public CallWrapper {
public:
    bool applies(const Syscall& sc) const override;
    void preCall(std::ofstream& os, const Syscall& sc) const override;
    void postCall(std::ofstream& os, const Syscall& sc, std::string return_var) const override;
};
