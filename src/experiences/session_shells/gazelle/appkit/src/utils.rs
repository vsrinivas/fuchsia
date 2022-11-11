// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_element as felement;
use fidl_fuchsia_sysmem as sysmem;
use fidl_fuchsia_ui_composition as ui_comp;
use fidl_fuchsia_ui_input3 as ui_input3;
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use fuchsia_component::client::connect_to_protocol;

/// Defines a trait to implement connecting to all services dependent by the app.
pub trait ProtocolConnector: Send {
    fn connect_to_flatland(&self) -> Result<ui_comp::FlatlandProxy, Error>;
    fn connect_to_graphical_presenter(&self) -> Result<felement::GraphicalPresenterProxy, Error>;
    fn connect_to_shortcuts_registry(&self) -> Result<ui_shortcut2::RegistryProxy, Error>;
    fn connect_to_keyboard(&self) -> Result<ui_input3::KeyboardProxy, Error>;
    fn connect_to_sysmem_allocator(&self) -> Result<sysmem::AllocatorProxy, Error>;
    fn connect_to_flatland_allocator(&self) -> Result<ui_comp::AllocatorProxy, Error>;

    fn box_clone(&self) -> Box<dyn ProtocolConnector>;
}

/// Provides connecting to services available in the current execution environment.
#[derive(Clone)]
pub struct ProductionProtocolConnector();

impl ProtocolConnector for ProductionProtocolConnector {
    fn connect_to_flatland(&self) -> Result<ui_comp::FlatlandProxy, Error> {
        connect_to_protocol::<ui_comp::FlatlandMarker>()
    }

    fn connect_to_graphical_presenter(&self) -> Result<felement::GraphicalPresenterProxy, Error> {
        connect_to_protocol::<felement::GraphicalPresenterMarker>()
    }

    fn connect_to_shortcuts_registry(&self) -> Result<ui_shortcut2::RegistryProxy, Error> {
        connect_to_protocol::<ui_shortcut2::RegistryMarker>()
    }

    fn connect_to_keyboard(&self) -> Result<ui_input3::KeyboardProxy, Error> {
        connect_to_protocol::<ui_input3::KeyboardMarker>()
    }

    fn connect_to_sysmem_allocator(&self) -> Result<sysmem::AllocatorProxy, Error> {
        connect_to_protocol::<sysmem::AllocatorMarker>()
    }

    fn connect_to_flatland_allocator(&self) -> Result<ui_comp::AllocatorProxy, Error> {
        connect_to_protocol::<ui_comp::AllocatorMarker>()
    }

    fn box_clone(&self) -> Box<dyn ProtocolConnector> {
        Box::new(ProductionProtocolConnector())
    }
}
