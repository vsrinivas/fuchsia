// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/json.h"

#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace modular {

namespace {

void XdrNounConstraint(
    modular::XdrContext* const xdr,
    ModuleManifestSource::Entry::NounConstraint* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("types", &data->types);
}

void XdrEntry(modular::XdrContext* const xdr,
              ModuleManifestSource::Entry* const data) {
  xdr->Field("binary", &data->binary);
  xdr->Field("local_name", &data->local_name);
  xdr->Field("verb", &data->verb);
  xdr->Field("noun_constraints", &data->noun_constraints, XdrNounConstraint);
}

}  // namespace

bool ModuleManifestEntryFromJson(const std::string& json,
                                 ModuleManifestSource::Entry* entry) {
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
  if (!modular::XdrRead(&doc, entry, XdrEntry)) {
    return false;
  }
  return true;
}

void ModuleManifestEntryToJson(const ModuleManifestSource::Entry& entry,
                               std::string* json) {
  rapidjson::Document doc;
  ModuleManifestSource::Entry local_entry{entry};
  modular::XdrWrite(&doc, &local_entry, XdrEntry);

  *json = JsonValueToPrettyString(doc);
}

}  // namespace modular
