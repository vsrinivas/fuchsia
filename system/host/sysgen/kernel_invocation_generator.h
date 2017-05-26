// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "generator.h"

/* Generates the kernel invocation bindings. */
class KernelInvocationGenerator : public Generator {
public:
    KernelInvocationGenerator(const std::string& syscall_prefix, const std::string& return_var,
                              const std::string& return_type, const std::string& arg_prefix)
        : syscall_prefix_(syscall_prefix), return_var_(return_var),
          return_type_(return_type), arg_prefix_(arg_prefix) {}

    bool syscall(std::ofstream& os, const Syscall& sc) override;

private:
    const std::string syscall_prefix_;
    const std::string return_var_;
    const std::string return_type_;
    const std::string arg_prefix_;
};
