// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULE_RESOLVER_CPP_FORMATTING_H_
#define LIB_MODULE_RESOLVER_CPP_FORMATTING_H_

#include "lib/fidl/cpp/bindings/formatting.h"
#include "lib/module_resolver/fidl/daisy.fidl.h"

namespace modular {

std::ostream& operator<<(std::ostream& os, const Daisy& daisy);
std::ostream& operator<<(std::ostream& os, const Noun& noun);

}  // namespace modular

#endif  // LIB_MODULE_RESOLVER_CPP_FORMATTING_H_
