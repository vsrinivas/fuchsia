// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest/module_manifest_xdr.h"

namespace modular {
namespace {

void XdrParameterConstraint(XdrContext* const xdr,
                            fuchsia::modular::ParameterConstraint* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("type", &data->type);
}

void XdrModuleManifestIntentFilters(XdrContext* const xdr,
                                    fuchsia::modular::IntentFilter* const data) {
  xdr->Field("action", &data->action);
  xdr->Field("parameters", &data->parameter_constraints, XdrParameterConstraint);
}

}  // namespace

void XdrModuleManifest_v1(XdrContext* const xdr, fuchsia::modular::ModuleManifest* const data) {
  std::string dummy_binary;
  xdr->Field("binary", &dummy_binary);
  xdr->Field("suggestion_headline", &data->suggestion_headline);
  xdr->Field("composition_pattern", &data->composition_pattern);

  // Version 2 supports multiple actions using the "intent_filters" field. This
  // version supports just one with "action" and "parameters" fields, so we
  // put those in "IntentFilter[0]".
  data->intent_filters->resize(1);
  xdr->Field("action", &data->intent_filters->at(0).action);
  xdr->Field("parameters", &data->intent_filters->at(0).parameter_constraints,
             XdrParameterConstraint);
}

void XdrModuleManifest_v2(XdrContext* const xdr, fuchsia::modular::ModuleManifest* const data) {
  if (!xdr->Version(2)) {
    return;
  }
  xdr->Field("suggestion_headline", &data->suggestion_headline);
  xdr->Field("composition_pattern", &data->composition_pattern);
  xdr->Field("intent_filters", &data->intent_filters, XdrModuleManifestIntentFilters);
  xdr->Field("placeholder_color", &data->placeholder_color);
}

// New nullable fields may be added to latest version (i.e., no need to do a
// version bump). Removing old fields needs a new version, in which case all xdr
// filter for previous versions should be updated to provide a polyfill.
extern const XdrFilterType<fuchsia::modular::ModuleManifest> XdrModuleManifest[] = {
    XdrModuleManifest_v2,
    XdrModuleManifest_v1,
    nullptr,
};

}  // namespace modular
