// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "coded_ast.h"
#include "flat_ast.h"
#include "string_view.h"

namespace fidl {

// Methods or functions named "Emit..." are the actual interface to
// the tables output.

// Methods named "Generate..." directly generate tables output via the
// "Emit" routines.

// Methods named "Produce..." indirectly generate tables output by calling
// the Generate methods, and should not call the "Emit" functions
// directly.

class TablesGenerator {
public:
    explicit TablesGenerator(flat::Library* library) {}

    ~TablesGenerator() = default;

    std::ostringstream Produce();

private:
    std::ostringstream tables_file_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
