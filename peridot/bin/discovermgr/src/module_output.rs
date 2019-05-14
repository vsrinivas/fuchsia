// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::story_context_store::StoryContextStore,
    crate::utils,
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_app_discover::{
        ModuleIdentifier, ModuleOutputWriterRequest, ModuleOutputWriterRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

/// The ModuleOutputWriter protocol implementation.
pub struct ModuleOutputWriterService {
    /// The story id to which the module belongs.
    story_id: String,

    /// The module id in story |story_id| to which the output belongs.
    module_id: String,

    /// Reference to the context store.
    story_context_store: Arc<Mutex<StoryContextStore>>,
}

impl ModuleOutputWriterService {
    /// Create a new module writer instance from an identifier.
    pub fn new(
        story_context_store: Arc<Mutex<StoryContextStore>>,
        module: ModuleIdentifier,
    ) -> Result<Self, Error> {
        Ok(ModuleOutputWriterService {
            story_id: module.story_id.ok_or(format_err!("expected story id"))?,
            module_id: utils::encoded_module_path(
                module.module_path.ok_or(format_err!("expected mod path"))?,
            ),
            story_context_store,
        })
    }

    /// Handle a stream of ModuleOutputWriter requests.
    pub fn spawn(self, mut stream: ModuleOutputWriterRequestStream) {
        fasync::spawn(
            async move {
                while let Some(request) = await!(stream.try_next()).context(format!(
                    "Error running module output for {:?} {:?}",
                    self.story_id, self.module_id,
                ))? {
                    match request {
                        ModuleOutputWriterRequest::Write {
                            output_name,
                            entity_reference,
                            responder,
                        } => {
                            self.handle_write(output_name, entity_reference);
                            responder.send(&mut Ok(()))?;
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| fx_log_err!("error serving module output {}", e)),
        )
    }

    /// Write to the given |entity_reference| to the context store and associate
    /// it to this module output |output_name|. If no entity reference is given,
    /// clear that output.
    fn handle_write(&self, output_name: String, entity_reference: Option<String>) {
        // TODO(miguelfrde): verify the output_name matches an output in
        // the manifest.
        fx_log_info!(
            "Got write for parameter name:{}, story:{}, mod:{:?}",
            output_name,
            self.story_id,
            self.module_id,
        );
        match entity_reference {
            // TODO(miguelfrde): veirfy the reference exists.
            Some(reference) => self.story_context_store.lock().contribute(
                &self.story_id,
                &self.module_id,
                &output_name,
                &reference,
            ),
            None => self.story_context_store.lock().withdraw(
                &self.story_id,
                &self.module_id,
                vec![&output_name],
            ),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::story_context_store::{ContextEntity, Contributor};
    use fidl_fuchsia_app_discover::ModuleOutputWriterMarker;

    #[fasync::run_until_stalled(test)]
    async fn test_write() {
        let state = Arc::new(Mutex::new(StoryContextStore::new()));

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ModuleOutputWriterMarker>().unwrap();
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };
        ModuleOutputWriterService::new(state.clone(), module).unwrap().spawn(request_stream);

        // Write a module output.
        assert!(await!(client.write("param-foo", Some("foo"))).is_ok());

        // Verify we have one entity with the right contributor.
        {
            let context_store = state.lock();
            let result = context_store.current().collect::<Vec<&ContextEntity>>();
            let mut expected_entity = ContextEntity::new("foo");
            expected_entity.add_contributor(Contributor::module_new(
                "story1",
                "mod-a",
                "param-foo",
            ));
            assert_eq!(result.len(), 1);
            assert_eq!(result[0], &expected_entity);
        }

        // Write no entity to the same output. This should withdraw the entity.
        assert!(await!(client.write("param-foo", None)).is_ok());

        // Verify we have no values.
        let context_store = state.lock();
        let result = context_store.current().collect::<Vec<&ContextEntity>>();
        assert_eq!(result.len(), 0);
    }
}
