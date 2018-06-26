// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/xdr.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

namespace {

void XdrParameterConstraint_v2(
    XdrContext* const xdr, fuchsia::modular::ParameterConstraint* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("type", &data->type);
}

void XdrModuleManifest_v1(XdrContext* const xdr,
                          fuchsia::modular::ModuleManifest* const data) {
  xdr->Field("binary", &data->binary);
  xdr->Field("suggestion_headline", &data->suggestion_headline);

  data->composition_pattern = "";
  data->action = "";
  data->parameter_constraints = nullptr;
}

// Composition Pattern was added in
// https://fuchsia-review.googlesource.com/c/peridot/+/130421
//
// Noun Constraint types were changed to singular in
// https://fuchsia-review.googlesource.com/c/peridot/+/136720
//
// Verb and Noun Constraints were renamed to fuchsia::modular::Action and
// Parameters in https://fuchsia-review.googlesource.com/c/peridot/+/147214
//
// We could have more backwards compatibility versions, but it doesn't seem
// necessary.
void XdrModuleManifest_v2(XdrContext* const xdr,
                          fuchsia::modular::ModuleManifest* const data) {
  xdr->Field("binary", &data->binary);
  xdr->Field("suggestion_headline", &data->suggestion_headline);
  xdr->Field("action", &data->action);
  xdr->Field("composition_pattern", &data->composition_pattern);
  xdr->Field("parameters", &data->parameter_constraints,
             XdrParameterConstraint_v2);
}

void XdrModuleManifest_v3(XdrContext* const xdr,
                          fuchsia::modular::ModuleManifest* const data) {
  if (!xdr->Version(3)) {
    return;
  }
  xdr->Field("binary", &data->binary);
  xdr->Field("suggestion_headline", &data->suggestion_headline);
  xdr->Field("action", &data->action);
  xdr->Field("composition_pattern", &data->composition_pattern);
  xdr->Field("parameters", &data->parameter_constraints,
             XdrParameterConstraint_v2);
}

}  // namespace

extern const XdrFilterType<fuchsia::modular::ModuleManifest>
    XdrModuleManifest[] = {
        XdrModuleManifest_v3,
        XdrModuleManifest_v2,
        XdrModuleManifest_v1,
        nullptr,
};

}  // namespace modular
