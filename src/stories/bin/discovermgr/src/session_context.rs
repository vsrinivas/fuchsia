// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{story_manager::StoryManager, utils},
    failure::{Error, ResultExt},
    fidl_fuchsia_app_discover::{
        SessionDiscoverContextRequest, SessionDiscoverContextRequestStream,
        StoryDiscoverContextRequest, StoryDiscoverContextRequestStream, StoryDiscoverError,
        SurfaceData,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

pub async fn run_server(
    mut stream: SessionDiscoverContextRequestStream,
    story_manager: Arc<Mutex<StoryManager>>,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("Error running session context")? {
        match request {
            SessionDiscoverContextRequest::GetStoryContext { story_id, request, .. } => {
                let story_context_stream = request.into_stream()?;
                StoryContextService::new(story_id, story_manager.clone())
                    .spawn(story_context_stream);
            }
        }
    }
    Ok(())
}

/// The StoryDiscoverContext protocol implementation.
pub struct StoryContextService {
    /// The story id to which the module belongs.
    story_id: String,
    story_manager: Arc<Mutex<StoryManager>>,
}

impl StoryContextService {
    pub fn new(story_id: impl Into<String>, story_manager: Arc<Mutex<StoryManager>>) -> Self {
        StoryContextService { story_id: story_id.into(), story_manager }
    }

    pub fn spawn(self, mut stream: StoryDiscoverContextRequestStream) {
        fasync::spawn_local(
            async move {
                while let Some(request) = stream
                    .try_next()
                    .await
                    .context(format!("Error running story context for {:?}", self.story_id))?
                {
                    match request {
                        StoryDiscoverContextRequest::GetSurfaceData { surface_id, responder } => {
                            // TODO: actually return the proper data.
                            let manager_lock = self.story_manager.lock();
                            let graph_result = manager_lock.get_story_graph(&self.story_id).await;
                            let result = graph_result
                                .as_ref()
                                .and_then(|result| result.get_module_data(&surface_id));
                            match result {
                                None => {
                                    responder.send(SurfaceData {
                                        action: None,
                                        parameter_types: None,
                                    })?;
                                }
                                Some(ref module_data) => {
                                    responder.send(SurfaceData {
                                        action: module_data.last_intent.action.clone(),
                                        // TODO: story_manager still doesn't contain the outputs
                                        parameter_types: Some(vec![]),
                                    })?;
                                }
                            }
                        }
                        StoryDiscoverContextRequest::SetProperty { key, value, responder } => {
                            let mut story_manager = self.story_manager.lock();
                            let mut res = story_manager
                                .set_property(
                                    &self.story_id,
                                    &key,
                                    utils::vmo_buffer_to_string(Box::new(value))?,
                                )
                                .await;
                            responder.send(&mut res)?;
                        }

                        StoryDiscoverContextRequest::GetProperty { key, responder } => {
                            let story_manager = self.story_manager.lock();
                            let property = story_manager.get_property(&self.story_id, &key).await;
                            responder.send(&mut property.and_then(|content| {
                                utils::string_to_vmo_buffer(content)
                                    .map_err(|_| StoryDiscoverError::VmoStringConversion)
                            }))?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("error serving story context {}", e)),
        )
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            constants::{GRAPH_KEY, STATE_KEY, TITLE_KEY},
            models::AddModInfo,
            story_manager::StoryManager,
            story_storage::MemoryStorage,
            utils,
        },
        fidl_fuchsia_app_discover::{SessionDiscoverContextMarker, StoryDiscoverContextMarker},
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn story_context_get_surface_data() -> Result<(), Error> {
        // Initialize some fake state.
        let story_id = "my-story".to_string();
        let mod_name = "my-mod".to_string();
        let action_name = "my-action".to_string();
        let story_manager = Arc::new(Mutex::new(StoryManager::new(Box::new(MemoryStorage::new()))));
        let mut action = AddModInfo::new_raw(
            "some-component-url",
            Some(story_id.clone()),
            Some(mod_name.clone()),
        );
        action.intent.action = Some(action_name.clone());
        {
            let mut manager_lock = story_manager.lock();
            manager_lock.add_to_story_graph(&action, vec![]).await?;
        }

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<SessionDiscoverContextMarker>().unwrap();
        fasync::spawn_local(
            async move { run_server(request_stream, story_manager).await }
                .unwrap_or_else(|e: Error| eprintln!("error running server {}", e)),
        );

        // Get the story context
        let (story_context_proxy, server_end) =
            fidl::endpoints::create_proxy::<StoryDiscoverContextMarker>()?;
        assert!(client.get_story_context(&story_id, server_end).is_ok());

        let surface_data = story_context_proxy.get_surface_data(&mod_name).await?;
        assert_eq!(surface_data.action, Some(action_name));
        assert_eq!(surface_data.parameter_types, Some(vec![]));
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_set_property() -> Result<(), Error> {
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<SessionDiscoverContextMarker>().unwrap();
        let story_manager_arc =
            Arc::new(Mutex::new(StoryManager::new(Box::new(MemoryStorage::new()))));

        let cloned_story_manager_arc = story_manager_arc.clone();
        fasync::spawn_local(
            async move { run_server(request_stream, cloned_story_manager_arc).await }
                .unwrap_or_else(|e: Error| eprintln!("error running server {}", e)),
        );

        // Get the StoryDiscoverContext connection.
        let (story_discover_context_proxy, server_end) =
            fidl::endpoints::create_proxy::<StoryDiscoverContextMarker>()?;
        assert!(client.get_story_context("story_name", server_end).is_ok());

        // Set the title of the story via SetProperty service
        assert!(story_discover_context_proxy
            .set_property(TITLE_KEY, &mut utils::string_to_vmo_buffer("new_title")?)
            .await
            .is_ok());

        // Get the title of the story via GetProperty service
        let returned_title = utils::vmo_buffer_to_string(Box::new(
            story_discover_context_proxy.get_property(TITLE_KEY).await?.unwrap(),
        ))?;

        // Ensure that set & get all succeed
        assert_eq!(returned_title, "new_title".to_string());

        // Ensure that setting to state/graph is not allowed
        assert_eq!(
            story_discover_context_proxy
                .set_property(STATE_KEY, &mut utils::string_to_vmo_buffer("some-state")?)
                .await?,
            Err(StoryDiscoverError::InvalidKey)
        );
        assert_eq!(
            story_discover_context_proxy
                .set_property(GRAPH_KEY, &mut utils::string_to_vmo_buffer("some-state")?)
                .await?,
            Err(StoryDiscoverError::InvalidKey)
        );
        Ok(())
    }
}
