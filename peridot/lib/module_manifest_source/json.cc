// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/json.h"

#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/module_manifest/module_manifest_xdr.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "rapidjson/document.h"

namespace modular {

bool ModuleManifestEntryFromJson(const std::string& json, fuchsia::modular::ModuleManifest* entry) {
  rapidjson::Document doc;
  // Schema validation of the JSON is happening at publish time. By the time we
  // get here, we assume it's valid manifest JSON.
  doc.Parse(json.c_str());

  // Handle bad manifests, including older files expressed as an array.
  // Any mismatch causes XdrRead to DCHECK.
  if (!doc.IsObject()) {
    return false;
  }

  // Our tooling validates |doc|'s JSON schema so that we don't have to here.
  // It may be good to do this, though.
  // TODO(thatguy): Do this if it becomes a problem.
  if (!XdrRead(&doc, entry, XdrModuleManifest)) {
    return false;
  }
  return true;
}

void ModuleManifestEntryToJson(const fuchsia::modular::ModuleManifest& entry, std::string* json) {
  rapidjson::Document doc;
  fuchsia::modular::ModuleManifest local_entry;
  fidl::Clone(entry, &local_entry);
  XdrWrite(&doc, &local_entry, XdrModuleManifest);

  *json = JsonValueToPrettyString(doc);
}

}  // namespace modular
