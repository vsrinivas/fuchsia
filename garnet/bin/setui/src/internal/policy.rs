// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message_hub_definition;
use crate::policy::{Address, Payload, Role};

// TODO(fxbug.dev/68487): remove once the policy layer uses the service message hub.
message_hub_definition!(Payload, Address, Role);
