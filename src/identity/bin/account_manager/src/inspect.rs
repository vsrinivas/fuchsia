// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Customized and easy to use types for exporting account_manager state via fuchsia-inspect.
//!
//! This module defines the format and meaning of all the inspect information published by
//! account_manager.

use fuchsia_inspect::{Node, StringProperty, UintProperty};

/// An abstraction over the properties of the account set that we expose via fuchsia-inspect.
pub struct Accounts {
    /// The underlying inspect node.
    _node: Node,
    /// The total number of accounts on the device.
    pub total: UintProperty,
    /// The number of active accounts on the device, i.e. the number with a running AccountHandler.
    pub active: UintProperty,
}

// TODO(jsankey): Write a procedural macro and trait to auto generate this implementation once it's
// stable.
impl Accounts {
    /// Creates a new Accounts as a child of the supplied node.
    pub fn new(parent: &Node) -> Self {
        let node = parent.create_child("accounts");
        let total = node.create_uint("total", 0);
        let active = node.create_uint("active", 0);
        Accounts { _node: node, total, active }
    }
}

/// An abstraction over the properties of the event listeners that we expose via fuchsia-inspect.
pub struct Listeners {
    /// The underlying inspect node.
    _node: Node,
    /// The total number of listeners that have been registered (including active).
    pub total_opened: UintProperty,
    /// The number of listeners currently registered to receive events.
    /// NOTE: The active count does not decrement automatically when a client disconnects, but it
    /// will eventually decrement. Inspect clients should assume eventual consistency.
    pub active: UintProperty,
    /// The total number of listen-able events that have occurred.
    pub events: UintProperty,
}

// TODO(jsankey): Write a procedural macro and trait to auto generate this implementation once it's
// stable.
impl Listeners {
    /// Creates a new Listeners as a child of the supplied node.
    pub fn new(parent: &Node) -> Self {
        let node = parent.create_child("listeners");
        let active = node.create_uint("active", 0);
        let total_opened = node.create_uint("total_opened", 0);
        let events = node.create_uint("events", 0);
        Listeners { _node: node, active, total_opened, events }
    }
}

/// An abstraction over the properties of the auth providers that we expose via fuchsia-inspect.
pub struct AuthProviders {
    /// The underlying inspect node.
    _node: Node,
    /// A comma separated list of the installed auth providers.
    pub types: StringProperty,
}

// TODO(jsankey): Write a procedural macro and trait to auto generate this implementation once it's
// stable.
impl AuthProviders {
    /// Creates a new AuthProviders as a child of the supplied node.
    pub fn new(parent: &Node) -> Self {
        let node = parent.create_child("auth_providers");
        let types = node.create_string("types", "");
        AuthProviders { _node: node, types }
    }
}
