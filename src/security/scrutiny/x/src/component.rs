// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::api::Component as ComponentApi;
use super::api::PackageResolverUrl;
use super::component_capability::ComponentCapability;
use super::component_instance::ComponentInstance;
use super::package::Package;
use std::iter;

// TODO(fxbug.dev/111243): Implement production component API.
#[derive(Default)]
pub(crate) struct Component;

impl ComponentApi for Component {
    type Package = Package;
    type ComponentCapability = ComponentCapability;
    type ComponentInstance = ComponentInstance;

    fn packages(&self) -> Box<dyn Iterator<Item = Self::Package>> {
        Box::new(iter::empty())
    }

    fn children(&self) -> Box<dyn Iterator<Item = PackageResolverUrl>> {
        Box::new(iter::empty())
    }

    fn uses(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
        Box::new(iter::empty())
    }

    fn exposes(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
        Box::new(iter::empty())
    }

    fn offers(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
        Box::new(iter::empty())
    }

    fn capabilities(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
        Box::new(iter::empty())
    }

    fn instances(&self) -> Box<dyn Iterator<Item = Self::ComponentInstance>> {
        Box::new(iter::empty())
    }
}
