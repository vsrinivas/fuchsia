// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::api::ComponentManagerConfiguration as ComponentManagerConfigurationApi;
use super::api::DevMgrConfiguration as DevMgrConfigurationApi;
use super::api::KernelFlags as KernelFlagsApi;
use super::api::System as SystemApi;
use super::api::VbMeta as VbMetaApi;
use super::api::Zbi as ZbiApi;
use super::blob::Blob;
use super::package::Package;
use std::iter;

/// TODO(fxbug.dev/111251): Implement production System API.
#[derive(Default)]
pub(crate) struct System;

impl SystemApi for System {
    type DataSourcePath = &'static str;
    type Zbi = Zbi;
    type Blob = Blob;
    type Package = Package;
    type KernelFlags = KernelFlags;
    type VbMeta = VbMeta;
    type DevMgrConfiguration = DevMgrConfiguration;
    type ComponentManagerConfiguration = ComponentManagerConfiguration;

    fn build_dir(&self) -> Self::DataSourcePath {
        ""
    }

    fn zbi(&self) -> Self::Zbi {
        Zbi::default()
    }

    fn update_package(&self) -> Self::Package {
        Package::default()
    }

    fn kernel_flags(&self) -> Self::KernelFlags {
        KernelFlags::default()
    }

    fn vb_meta(&self) -> Self::VbMeta {
        VbMeta::default()
    }

    fn devmgr_configuration(&self) -> Self::DevMgrConfiguration {
        DevMgrConfiguration::default()
    }

    fn component_manager_configuration(&self) -> Self::ComponentManagerConfiguration {
        ComponentManagerConfiguration::default()
    }
}

/// TODO(fxbug.dev/111251): Implement for production System API.
#[derive(Default)]
pub(crate) struct Zbi;

impl ZbiApi for Zbi {
    type BootfsPath = &'static str;
    type Blob = Blob;

    fn bootfs(&self) -> Box<dyn Iterator<Item = (Self::BootfsPath, Self::Blob)>> {
        Box::new(iter::empty())
    }
}

/// TODO(fxbug.dev/111251): Implement for production System API.
#[derive(Default)]
pub(crate) struct KernelFlags;

impl KernelFlagsApi for KernelFlags {
    fn get(&self, _key: &str) -> Option<&str> {
        None
    }

    fn iter(&self) -> Box<dyn Iterator<Item = (String, String)>> {
        Box::new(iter::empty())
    }
}

/// TODO(fxbug.dev/111251): Implement for production System API.
#[derive(Default)]
pub(crate) struct VbMeta;

impl VbMetaApi for VbMeta {}

/// TODO(fxbug.dev/111251): Implement for production System API.
#[derive(Default)]
pub(crate) struct DevMgrConfiguration;

impl DevMgrConfigurationApi for DevMgrConfiguration {
    fn get(&self, _key: &str) -> Option<&str> {
        None
    }

    fn iter(&self) -> Box<dyn Iterator<Item = (String, String)>> {
        Box::new(iter::empty())
    }
}

/// TODO(fxbug.dev/111251): Implement for production System API.
#[derive(Default)]
pub(crate) struct ComponentManagerConfiguration;

impl ComponentManagerConfigurationApi for ComponentManagerConfiguration {}
