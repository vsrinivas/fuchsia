// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_CONTEXT_H_
#define APPS_MODULAR_LIB_FIDL_CONTEXT_H_

#include "lib/fidl/cpp/bindings/map.h"
#include "lib/fidl/cpp/bindings/string.h"

namespace modular {

// This type appears on the values field in struct maxwell::ContentUpdate, but
// modular uses the value of this field standalone as argument of functions and
// data member, so we abbreviate its type to reduce verbosity slightly.
//
// The map is from context topic to its json value.
using ContextState = fidl::Map<fidl::String, fidl::String>;

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_CONTEXT_H_
