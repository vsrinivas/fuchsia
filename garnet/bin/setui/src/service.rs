// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Service-wide MessageHub definition.
//!
//! The service mod defines a MessageHub (and associated Address, Payload, and
//! Role namespaces) to facilitate service-wide communication. All
//! communication, both intra-component and inter-component, should be sent
//! through this hub. The address space of this MessageHub allows any component
//! to be reached when given a public address. The Role space allows granular
//! message filtering and audience targeting.
//!
//! The static Address and Role definitions below provide a way to reference
//! such values at build time. However, many use-cases that rely on these
//! features can be done through values generated at runtime instead. Care
//! should be taken before expanding either enumeration.
//!
//! Currently, service communication happens in a number of domain-specific
//! message hubs located in the internal mod. Communication from these hubs
//! should migrate here over time.

use crate::base::SettingType;
use crate::message_hub_definition;

message_hub_definition!(Payload, Address, Role);

/// The `Address` enumeration defines a namespace for entities that can be
/// reached by a predefined name. Care should be taken when adding new child
/// namespaces here. Each address can only belong to a single entity.
/// Most communication can be instead facilitated with a messenger's signature,
/// which is available at messenger creation time.
#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Address {
    Handler(SettingType),
}

/// The types of data that can be sent through the service [`MessageHub`]. This
/// enumeration is meant to provide a top level definition. Further definitions
/// for particular domains should be located in the appropriate mod.
#[derive(Clone, Debug)]
pub enum Payload {}

/// `Role` defines grouping for responsibilities within the service. Roles allow
/// for addressing a particular audience space. Similar to the other
/// enumerations, details about each role should be defined near to the domain.
#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Role {}
