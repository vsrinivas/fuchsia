// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::api::ComponentManager as ComponentManagerApi;
use super::component_capability::ComponentCapability;
use super::system::ComponentManagerConfiguration;
use std::iter;

/// TODO(fxbug.dev/111251): Implement for production System API.
#[derive(Default)]
pub(crate) struct ComponentManager;

impl ComponentManagerApi for ComponentManager {
    type ComponentManagerConfiguration = ComponentManagerConfiguration;
    type ComponentCapability = ComponentCapability;

    fn configuration(&self) -> Self::ComponentManagerConfiguration {
        ComponentManagerConfiguration::default()
    }

    fn namespace_capabilities(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
        Box::new(iter::empty())
    }

    fn builtin_capabilities(&self) -> Box<dyn Iterator<Item = Self::ComponentCapability>> {
        Box::new(iter::empty())
    }
}
