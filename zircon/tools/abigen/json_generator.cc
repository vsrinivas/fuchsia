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
  for (std::vector<std::string>::size_type index = 0; index != sc.attributes.size(); ++index) {
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

    // The .json output is currently only used by the syscall documentation updater, and it doesn't
    // use the array counts. Temporarily disable this part of the output, so that kazoo and abigen's
    // output is identical.
#if 0
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
#endif

    // Attributes.
    // The .json output is currently only used by the syscall documentation updater, and it only
    // needs "IN" argument attributes to add "const". Other arguments are tagged OUT/INOUT, but
    // don't completely match what kazoo would output. Rather than modifying abigen to match kazoo,
    // or adding a lot of unused logic to kazoo, only output "IN" when specified to make kazoo and
    // abigen's json output match.
    //
    // So: the attributes output will either be `"attributes":[]` or
    // `"attributes": ["IN"]`, but no other argument attributes are output.
    bool has_in = false;
    for (const auto& attrib : arg.attributes) {
      if (attrib == "IN") {
        has_in = true;
      }
    }
    os << "          \"attributes\": [\n";
    if (has_in) {
      os << "            \"IN\"\n";
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
