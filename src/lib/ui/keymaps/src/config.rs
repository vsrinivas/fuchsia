// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Keymap configuration store.
//!
//! Used to load and store keymap configurations from configuration files.

use fidl_fuchsia_input as input;
use fidl_fuchsia_ui_input3 as input3; // Experimenting with really short aliases.

/// The data model for a single keymap configuration.
#[derive(Debug, Clone)]
pub struct Model {
    /// Auxiliary information for this model.
    #[allow(unused)]
    metadata: Metadata,

    /// Mapping of hardware keyboard symbol sequences to key meanings.
    ///
    /// Symbol mappings are tried in the sequence they appear in this vector, and the first one
    /// that is satisfied will be applied.
    #[allow(unused)]
    symbol_mappings: Vec<SymbolMapping>,
}

#[derive(Debug, Clone)]
pub struct Metadata {
    /// The identifier for this keymap. Should be unique across all accessible
    /// keymaps.  Obviously, we'll need to work on that.
    pub keymap_id: String,
}

/// How a key chord is mapped to a symbol.
///
/// The encoding is not very compact, and could be repetitive.  For example,
/// you will need separate mappings for left shift and right shift keys, even
/// though their effects are typically the same. (It also helps if you
/// want their effects to be *different*.)
#[derive(Debug, Clone)]
pub struct SymbolMapping {
    /// The set of keys that *must* be actuated for this symbol mapping to take
    /// effect.
    #[allow(unused)]
    modifiers_armed: Vec<input::Key>,

    /// The set of keys that *may* be actuated while `modifiers_armed` are actuated,
    /// for this symbol mapping to take effect.
    #[allow(unused)]
    modifiers_optional: Vec<input::Key>,

    /// When all `modifers_armed` are actuated, and any subset of `modifiers_optional` are
    /// actuated, `mappings` shows how each additional key press maps to a `KeyMeaning`.
    #[allow(unused)]
    mappings: Vec<SymbolMappingElem>,
}

/// A single mapping from a physical key event into a key meaning.  We decode
/// both key presses and key releases, so the actual key event type is not part
/// of the mapping.
#[derive(Debug, Clone)]
pub struct SymbolMappingElem {
    /// The physical key that was pressed.
    #[allow(unused)]
    key: input::Key,

    /// The translation of `key` into a key meaning.
    #[allow(unused)]
    key_meaning: input3::KeyMeaning,
}
