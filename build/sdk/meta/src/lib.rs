// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate lazy_static;

#[macro_use]
pub mod testing;
mod banjo_library;
mod cc_prebuilt_library;
mod cc_source_library;
mod common;
mod dart_library;
mod data;
mod documentation;
mod fidl_library;
mod host_tool;
mod json;
mod loadable_module;
mod manifest;
mod metadata;
mod product_bundle;
mod product_bundle_container;
mod sysroot;

// These need to be addressable from external code, because they have conflicting types
// named "Hardware" and "Cpu". In order to use one of these types in external code, it
// needs to specify which version of the type to use, e.g. virtual_device::Hardware, or
// the import will fail to locate the type.
pub mod physical_device;
pub mod virtual_device;

pub use crate::banjo_library::*;
pub use crate::cc_prebuilt_library::*;
pub use crate::cc_source_library::*;
pub use crate::common::*;
pub use crate::dart_library::*;
pub use crate::data::*;
pub use crate::documentation::*;
pub use crate::fidl_library::*;
pub use crate::host_tool::*;
pub use crate::json::JsonObject;
pub use crate::loadable_module::*;
pub use crate::manifest::*;
pub use crate::metadata::*;
pub use crate::physical_device::*;
pub use crate::product_bundle::*;
pub use crate::product_bundle_container::*;
pub use crate::sysroot::*;
pub use crate::virtual_device::*;
