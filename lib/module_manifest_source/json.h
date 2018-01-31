// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/module_manifest_source.h"

namespace modular {

bool ModuleManifestEntryFromJson(const std::string& json, ModuleManifestSource::Entry* entry);
void ModuleManifestEntryToJson(const ModuleManifestSource::Entry& entry, std::string* json);

}  // namespace modular
