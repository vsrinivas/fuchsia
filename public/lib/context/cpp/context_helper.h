// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONTEXT_CPP_CONTEXT_HELPER_H_
#define LIB_CONTEXT_CPP_CONTEXT_HELPER_H_

#include <utility>

#include <fuchsia/cpp/modular.h>

#include "lib/fidl/cpp/vector.h"

namespace maxwell {

std::pair<bool, fidl::VectorPtr<modular::ContextValue>> TakeContextValue(
    modular::ContextUpdate* update, const std::string& key);

void AddToContextQuery(modular::ContextQuery* query, const std::string& key,
                       modular::ContextSelector selector);

bool HasSelectorKey(modular::ContextQuery* const query, const std::string& key);

}  // namespace maxwell

#endif  // LIB_CONTEXT_CPP_CONTEXT_HELPER_H_
