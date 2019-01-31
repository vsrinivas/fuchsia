// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "generator.h"

bool JsonGenerator::header(std::ofstream& os) {
    os << "{\n";
    os << "  \"syscalls\": [\n";
    return os.good();
}

bool JsonGenerator::footer(std::ofstream& os) {
    os << "\n";
    os << "  ]\n";
    os << "}\n";
    return os.good();
}

bool JsonGenerator::syscall(std::ofstream& os, const Syscall& sc) {
    if (first_syscall_) {
        first_syscall_ = false;
    } else {
        os << ",\n";
    }
    os << "    {\n";
    os << "      \"name\": \"" << sc.name << "\",\n";

    // Attributes.
    os << "      \"attributes\": [\n";
    for (std::vector<std::string>::size_type index = 0;
         index != sc.attributes.size(); ++index) {
        os << "        \"" << sc.attributes[index] << "\"";
        if (index < sc.attributes.size() - 1) {
            os << ",";
        }
        os << "\n";
    }
    os << "      ],\n";

    // Top description.
    os << "      \"top_description\": [\n";
    os << "        ";
    for (size_t i = 0; i < sc.top_description.size(); ++i) {
        os << "\"" << sc.top_description[i] << "\"";
        if (i < sc.top_description.size() - 1) {
            os << ", ";
        }
    }
    os << "\n      ],\n";

    // Requirements.
    os << "      \"requirements\": [\n";
    for (size_t i = 0; i < sc.requirements.size(); ++i) {
        os << "        ";
        for (size_t j = 0; j < sc.requirements[i].size(); ++j) {
            os << "\"" << sc.requirements[i][j] << "\"";
            if (j < sc.requirements[i].size() - 1) {
                os << ", ";
            }
        }
        if (i < sc.requirements.size() - 1) {
            os << ",";
        }
        os << "\n";
    }
    os << "      ],\n";

    // Arguments.
    os << "      \"arguments\": [\n";
    bool first_arg = true;
    bool has_args = false;
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        has_args = true;
        if (first_arg) {
            first_arg = false;
        } else {
            os << ",\n";
        }
        os << "        {\n";
        os << "          \"name\": \"" << arg.name << "\",\n";
        os << "          \"type\": \"" << arg.type << "\",\n";

        // Array spec.
        os << "          \"is_array\": " << (arg.arr_spec ? "true" : "false") << ",\n";
        if (arg.arr_spec) {
            if (arg.arr_spec->count) {
                os << "          \"array_count\": " << arg.arr_spec->count << ",\n";
            } else {
                os << "          \"array_multipliers\": [\n";
                for (std::vector<std::string>::size_type index = 0;
                     index != arg.arr_spec->multipliers.size(); ++index) {
                    os << "            \"" << arg.arr_spec->multipliers[index] << "\"";
                    if (index < arg.arr_spec->multipliers.size() - 1) {
                        os << ",";
                    }
                    os << "\n";
                }
                os << "          ],\n";
            }
        }

        // Attributes.
        os << "          \"attributes\": [\n";
        for (std::vector<std::string>::size_type index = 0;
             index != arg.attributes.size(); ++index) {
            os << "            \"" << arg.attributes[index] << "\"";
            if (index < arg.attributes.size() - 1) {
                os << ",";
            }
            os << "\n";
        }
        os << "          ]\n";
        os << "        }";
    });
    if (has_args) {
        os << "\n";
    }
    os << "      ],\n";

    os << "      \"return_type\": \"" << sc.return_type() << "\"\n";

    os << "    }";
    return os.good();
}
