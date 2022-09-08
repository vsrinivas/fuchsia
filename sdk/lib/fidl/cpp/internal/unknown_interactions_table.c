// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/unknown_interactions_table.h"

static const struct FidlXUnionField kUnknownMethodResponseFields[] = {
    {.type = NULL}, {.type = NULL}, {.type = (fidl_type_t*)(&fidl_internal_kTransportErrTable)}};

const struct FidlCodedXUnion kFidlInternalUnknownMethodResponseTable = {
    .tag = kFidlTypeXUnion,
    .nullable = kFidlNullability_Nonnullable,
    .strictness = kFidlStrictness_Strict,
    .is_resource = kFidlIsResource_NotResource,
    .field_count = 3u,
    .fields = kUnknownMethodResponseFields,
    .name = "fidl/UnknownInteractionResult",
};
