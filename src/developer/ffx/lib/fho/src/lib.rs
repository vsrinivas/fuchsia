// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

mod search;
mod subtool;
pub mod testing;

pub use search::*;
pub use subtool::*;

// Used for deriving an FFX tool.
pub use fho_macro::FfxTool;

/// Versions of FHO and their extended metadata
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Hash)]
pub enum FhoVersion {
    /// Run the command as if it were a normal ffx invocation, with
    /// no real protocol to speak of. This is a transitionary option,
    /// and will be removed before we're ready to land external tools
    /// in the sdk.
    FhoVersion0 {},
}
impl Default for FhoVersion {
    fn default() -> Self {
        FhoVersion::FhoVersion0 {}
    }
}

/// Metadata about an FHO-compliant ffx subtool
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Hash)]
pub struct FhoToolMetadata {
    /// The name of the subtool. Should be the same as the executable binary
    name: String,
    /// A brief description of the subtool. Should be one line long and suitable
    /// for including in help output.
    description: String,
    /// The minimum fho version this tool can support (details will be the maximum)
    requires_fho: u16,
    /// Further details about the tool's expected FHO interface version.
    fho_details: FhoVersion,
}

impl FhoToolMetadata {
    /// Creates new metadata aligned to the current version and expectations of fho
    pub fn new(name: &str, description: &str) -> Self {
        let name = name.to_owned();
        let description = description.to_owned();
        let requires_fho = 0;
        let fho_details = Default::default();
        Self { name, description, requires_fho, fho_details }
    }
}

#[doc(hidden)]
pub mod macro_deps {
    pub use ffx_command::{FfxCommandLine, ToolRunner};
    pub use ffx_config::EnvironmentContext;
    pub use ffx_core::Injector;
    pub use futures;
    pub use serde;
}
