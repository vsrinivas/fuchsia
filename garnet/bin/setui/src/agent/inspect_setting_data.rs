// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::blueprint_definition;

blueprint_definition!(
    "inspect_setting_data",
    crate::inspect::inspect_broker::InspectSettingAgent::create
);
