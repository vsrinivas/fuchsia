// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "generator.h"

bool RustBindingGenerator::header(std::ofstream& os) {
    if (!Generator::header(os)) {
        return false;
    }
    os << "#[link(name = \"magenta\")]\n";
    os << "extern {\n";
    return os.good();
}

bool RustBindingGenerator::footer(std::ofstream& os) {
    if (!Generator::footer(os)) {
        return false;
    }
    os << "}\n";
    return os.good();
}

bool RustBindingGenerator::syscall(std::ofstream& os, const Syscall& sc) {
    os << "    pub fn mx_" << sc.name << "(";

    // Writes all arguments.
    sc.for_each_kernel_arg([&](const TypeSpec& arg) {
        os << "\n        "
           << arg.as_rust_declaration() << ",";
    });

    if (!os.good()) {
        return false;
    }

    if (sc.num_kernel_args() > 0) {
        // remove the comma.
        os.seekp(-1, std::ios_base::end);
    }
    // Finish off list and write return type
    os << "\n        )";
    if (sc.return_type() != "void") {
        os << " -> " << map_override(sc.return_type(), rust_primitives);
    }
    os << ";\n\n";

    return os.good();
}
