// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "header_generator.h"

using std::map;
using std::string;

static const string add_attribute(map<string, string> attributes,
                                  const string& attribute) {
    auto ft = attributes.find(attribute);
    return (ft == attributes.end()) ? string() : ft->second;
}

bool HeaderGenerator::syscall(std::ofstream& os, const Syscall& sc) {
    if (skip_vdso_calls_ && sc.is_vdso()) {
        return true;
    }

    constexpr uint32_t indent_spaces = 4u;

    for (auto name_prefix : name_prefixes_) {
        auto syscall_name = name_prefix + sc.name;

        os << function_prefix_;

        write_syscall_signature_line(os, sc, name_prefix, "\n", "\n" + string(indent_spaces, ' '),
                                     allow_pointer_wrapping_ && !sc.is_no_wrap() && !sc.is_vdso(),
                                     no_args_type_);

        os << " ";

        // Writes attributes after arguments.
        for (const auto& attr : sc.attributes) {
            auto a = add_attribute(attributes_, attr);
            if (!a.empty())
                os << a << " ";
        }

        os.seekp(-1, std::ios_base::end);

        os << ";\n\n";

        syscall_name = "_" + syscall_name;
    }

    return os.good();
}
