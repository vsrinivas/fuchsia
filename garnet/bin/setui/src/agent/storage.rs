// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Agent for interacting with storage.
pub(crate) mod storage_agent;

/// Allows controllers to store state in persistent device level storage.
pub mod device_storage;

/// Storage that interacts with persistent fidl.
pub(crate) mod fidl_storage;
