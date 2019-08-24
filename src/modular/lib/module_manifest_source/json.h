// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULE_MANIFEST_SOURCE_JSON_H_
#define SRC_MODULAR_LIB_MODULE_MANIFEST_SOURCE_JSON_H_

#include "src/modular/lib/module_manifest_source/module_manifest_source.h"

namespace modular {

bool ModuleManifestEntryFromJson(const std::string& json, fuchsia::modular::ModuleManifest* entry);
void ModuleManifestEntryToJson(const fuchsia::modular::ModuleManifest& entry, std::string* json);

}  // namespace modular

#endif  // SRC_MODULAR_LIB_MODULE_MANIFEST_SOURCE_JSON_H_
