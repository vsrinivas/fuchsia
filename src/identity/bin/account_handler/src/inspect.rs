// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Customized and easy to use types for exporting account_handler state via fuchsia-inspect.
//!
//! This module defines the format and meaning of all the inspect information published by
//! account_handler.

use {
    account_common::{AccountId, PersonaId},
    fuchsia_inspect::{Node, StringProperty, UintProperty},
};

/// An abstraction over the properties of the account handler that we expose via fuchsia-inspect.
pub struct AccountHandler {
    /// The underlying inspect node.
    node: Node,
    /// The account id
    pub account_id: UintProperty,
    /// Represents the state of the account handler, such as "initialized"
    pub lifecycle: StringProperty,
}

impl AccountHandler {
    /// Creates a new AccountHandler as a child of the supplied node.
    pub fn new<'a>(parent: &'a Node, account_id: &'a AccountId, lifecycle: &'a str) -> Self {
        let node = parent.create_child("account_handler");
        let account_id = node.create_uint("account_id", account_id.clone().into());
        let lifecycle = node.create_string("lifecycle", lifecycle);
        Self { node, account_id, lifecycle }
    }

    /// Get the underlying node, can be used to create children.
    pub fn get_node(&self) -> &Node {
        &self.node
    }
}

/// An abstraction over the properties of the account that we expose via fuchsia-inspect.
pub struct Account {
    /// The underlying inspect node.
    _node: Node,
    /// The number of active clients of the Account.
    pub open_client_channels: UintProperty,
}

impl Account {
    /// Creates a new Account as a child of the supplied node.
    pub fn new(parent: &Node) -> Self {
        let node = parent.create_child("account");
        let open_client_channels = node.create_uint("open_client_channels", 0);
        Self { _node: node, open_client_channels }
    }
}

/// An abstraction over the properties of the persona that we expose via fuchsia-inspect.
pub struct Persona {
    /// The underlying inspect node.
    _node: Node,
    /// The persona id
    pub persona_id: UintProperty,
    /// The number of active clients of the Persona.
    pub open_client_channels: UintProperty,
}

impl Persona {
    /// Creates a new Persona as a child of the supplied node.
    pub fn new(parent: &Node, persona_id: &PersonaId) -> Self {
        let node = parent.create_child("default_persona");
        let persona_id = node.create_uint("persona_id", persona_id.clone().into());
        let open_client_channels = node.create_uint("open_client_channels", 0);
        Self { _node: node, persona_id, open_client_channels }
    }
}
