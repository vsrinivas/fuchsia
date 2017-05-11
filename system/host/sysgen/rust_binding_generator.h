// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "generator.h"

/* Generates the Rust bindings. */
class RustBindingGenerator : public Generator {
public:
  bool header(std::ofstream& os) override;
  bool footer(std::ofstream& os) override;
  bool syscall(std::ofstream& os, const Syscall& sc) override;
};
