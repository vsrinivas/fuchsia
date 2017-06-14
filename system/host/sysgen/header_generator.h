// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "generator.h"

/* Generates header files. */
class HeaderGenerator : public Generator {
public:
    // A prefix on the syscall name, and a predicate that returns
    // true if it should be omitted from the header.
    using name_prefix_map =
        std::vector<std::pair<std::string, bool (*)(const Syscall&)>>;

    HeaderGenerator(const std::string& function_prefix,
                    const name_prefix_map& name_prefixes,
                    const std::string& no_args_type,
                    bool allow_pointer_wrapping,
                    const std::map<std::string, std::string>& attributes)
        : function_prefix_(function_prefix),
          name_prefixes_(name_prefixes),
          no_args_type_(no_args_type),
          attributes_(attributes),
          allow_pointer_wrapping_(allow_pointer_wrapping) {}

    bool syscall(std::ofstream& os, const Syscall& sc) override;

private:
    const std::string function_prefix_;
    const name_prefix_map name_prefixes_;
    const std::string no_args_type_;
    const std::map<std::string, std::string> attributes_;
    const bool allow_pointer_wrapping_;
};

HeaderGenerator kernel_header_generator();
HeaderGenerator user_header_generator();
HeaderGenerator vdso_header_generator();
