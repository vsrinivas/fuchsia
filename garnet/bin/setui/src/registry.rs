// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The base mod defines the Registry trait, whose interface allows components
/// to handle setting requests. It also defines the basic communication building
/// blocks used with the Registry.
pub mod base;

/// This mod provides a concrete implementation of the Registry trait. It should
/// be shared as the trait beyond its construction.
pub mod registry_impl;

/// This mod allows controllers to store state in persistent device level storage.
pub mod device_storage;

/// This mod implements a factory that can be populated to provide the registry
/// with handlers on demand.
pub mod setting_handler_factory_impl;
