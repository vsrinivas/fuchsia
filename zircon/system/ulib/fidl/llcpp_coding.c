// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

// Coding table used for AnyZeroArgMessage, which is the message object used
// when there are no request / response parameters.
extern const struct FidlCodedStruct _llcpp_coding_AnyZeroArgMessageTable;

const struct FidlCodedStruct _llcpp_coding_AnyZeroArgMessageTable = {
    .tag = kFidlTypeStruct,
    .field_count = 0u,
    .size = 16u,
    .fields = NULL,
    .name = "AnyZeroArgMessage",
};
