// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_LINK_JSON_H_
#define APPS_MODULAR_SRC_USER_RUNNER_LINK_JSON_H_

#include "apps/modular/services/document_store/document.fidl.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fidl/cpp/bindings/map.h"

namespace modular {

using LinkData = fidl::Map<fidl::String, document_store::DocumentPtr>;

LinkData ConvertToLink(const rapidjson::Document& input);

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_LINK_JSON_H_
