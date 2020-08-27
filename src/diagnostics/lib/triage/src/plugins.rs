// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{act::ActionResults, metrics::FileDataFetcher};

mod crashes;

pub trait Plugin {
    /// Returns a unique name for the plugin.
    ///
    /// This is the value selected and listed on the command line.
    fn name(&self) -> &'static str;

    /// Returns a human-readable name for the plugin.
    ///
    /// This will be displayed as the label for results from this plugin.
    fn display_name(&self) -> &'static str;

    /// Run the plugin on the given inputs to produce results.
    fn run(&self, inputs: &FileDataFetcher<'_>) -> ActionResults;
}

/// Retrieve the list of all plugins registered with this library.
pub fn register_plugins() -> Vec<Box<dyn Plugin>> {
    vec![Box::new(crashes::CrashesPlugin {})]
}
