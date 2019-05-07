// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_app_discover::{
        ModuleIdentifier, ModuleOutputWriterRequest, ModuleOutputWriterRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    futures::prelude::*,
};

/// The ModuleOutputWriter protocol implementation.
pub struct ModuleOutputWriterService {
    /// The story id to which the module belongs.
    story_id: String,

    /// The module id in story |story_id| to which the output belongs.
    module_path: Vec<String>,
}

impl ModuleOutputWriterService {
    /// Create a new module writer instance from an identifier.
    pub fn new(module: ModuleIdentifier) -> Result<Self, Error> {
        Ok(ModuleOutputWriterService {
            story_id: module.story_id.ok_or(format_err!("expected story id"))?,
            module_path: module.module_path.ok_or(format_err!("expected mod path"))?,
        })
    }

    /// Handle a stream of ModuleOutputWriter requests.
    pub fn spawn(self, mut stream: ModuleOutputWriterRequestStream) {
        fasync::spawn(
            async move {
                while let Some(request) = await!(stream.try_next()).context(format!(
                    "Error running module output for {:?} {:?}",
                    self.story_id, self.module_path,
                ))? {
                    match request {
                        ModuleOutputWriterRequest::Write {
                            output_name,
                            entity_reference: _,
                            responder,
                        } => {
                            fx_log_info!(
                                "Got write for parameter name:{}, story:{}, mod:{:?}",
                                output_name,
                                self.story_id,
                                self.module_path,
                            );
                            responder.send(&mut Ok(()))?;
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| fx_log_err!("error serving module output {}", e)),
        )
    }
}
