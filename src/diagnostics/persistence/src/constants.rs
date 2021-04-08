// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Base for the service name used by clients to connect to their specific persist protocol..
/// The actual service name is defined by appending _<service name defined in config> to this constant.
pub const PERSIST_SERVICE_NAME_PREFIX: &str = "fuchsia.diagnostics.persist.DataPersistence";
