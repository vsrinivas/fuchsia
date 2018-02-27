// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONTEXT_CPP_CONTEXT_HELPER_H_
#define LIB_CONTEXT_CPP_CONTEXT_HELPER_H_

#include <utility>

#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"

namespace maxwell {

std::pair<bool, f1dl::Array<ContextValuePtr>> TakeContextValue(
    ContextUpdate* update, const std::string& key);

void AddToContextQuery(ContextQuery* query, const std::string& key,
                       ContextSelectorPtr selector);

}  // namespace maxwell

#endif  // LIB_CONTEXT_CPP_CONTEXT_HELPER_H_
