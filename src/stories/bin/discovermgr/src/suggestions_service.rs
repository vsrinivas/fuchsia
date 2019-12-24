// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        story_context_store::{ContextEntity, ContextReader},
        suggestions_manager::SuggestionsManager,
    },
    anyhow::{Context as _, Error},
    fidl_fuchsia_app_discover::{
        InteractionType, Suggestion, SuggestionsIteratorRequest, SuggestionsIteratorRequestStream,
        SuggestionsRequest, SuggestionsRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

// TODO: we should send the maximum number of suggestions in a vector that stays
// within the maxium FIDL message size.
const ITERATOR_CHUNK_SIZE: usize = 10;

pub struct SuggestionsService {
    context_store: Arc<Mutex<dyn ContextReader>>,
    suggestions_manager: Arc<Mutex<SuggestionsManager>>,
}

impl SuggestionsService {
    pub fn new(
        context_store: Arc<Mutex<dyn ContextReader>>,
        suggestions_manager: Arc<Mutex<SuggestionsManager>>,
    ) -> Self {
        SuggestionsService { context_store, suggestions_manager }
    }

    pub async fn handle_client(
        &mut self,
        mut stream: SuggestionsRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            stream.try_next().await.context("error running discover context")?
        {
            match request {
                SuggestionsRequest::GetSuggestions { query, iterator, .. } => {
                    let stream = iterator.into_stream()?;
                    self.serve_suggestions(query, stream).await;
                }
                SuggestionsRequest::NotifyInteraction { suggestion_id, interaction, .. } => {
                    match interaction {
                        InteractionType::Selected => {
                            let mut manager = self.suggestions_manager.lock();
                            if let Err(e) = manager.execute(&suggestion_id).await {
                                fx_log_err!("Error executing suggestion {}: {}", suggestion_id, e);
                            }
                        }
                    }
                }
            }
        }
        Ok(())
    }

    pub async fn serve_suggestions(
        &mut self,
        query: String,
        mut stream: SuggestionsIteratorRequestStream,
    ) {
        let context_store_lock = self.context_store.lock();
        let context = context_store_lock.current().collect::<Vec<&ContextEntity>>();
        let mut manager = self.suggestions_manager.lock();
        let suggestions = manager
            .get_suggestions(&query, &context)
            .await
            .map(|s| s.clone().into())
            .collect::<Vec<Suggestion>>();
        fasync::spawn(
            async move {
                let mut iter = suggestions.into_iter();
                while let Some(request) = stream.try_next().await? {
                    let SuggestionsIteratorRequest::Next { responder } = request;
                    responder.send(&mut iter.by_ref().take(ITERATOR_CHUNK_SIZE))?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| {
                fx_log_err!("error running suggestions iterator server: {}", e);
            }),
        );
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::testing::init_state,
        fidl_fuchsia_app_discover::{
            SuggestionsIteratorMarker, SuggestionsMarker, SuggestionsProxy,
        },
        fidl_fuchsia_modular::{EntityResolverMarker, PuppetMasterMarker},
    };

    fn setup() -> Result<SuggestionsProxy, Error> {
        let (puppet_master, _) = fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>()?;
        let (entity_resolver, _) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>()?;

        let (context_store, _, mod_manager) = init_state(puppet_master, entity_resolver);
        let suggestions_manager = Arc::new(Mutex::new(SuggestionsManager::new(mod_manager)));

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<SuggestionsMarker>()?;
        fasync::spawn_local(
            async move {
                let mut service = SuggestionsService::new(context_store, suggestions_manager);
                service.handle_client(request_stream).await
            }
            .unwrap_or_else(|e: Error| eprintln!("error running server {}", e)),
        );
        Ok(client)
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_suggestions() -> Result<(), Error> {
        let client = setup()?;

        let (iterator, server_end) = fidl::endpoints::create_proxy::<SuggestionsIteratorMarker>()?;
        assert!(client.get_suggestions("test", server_end).is_ok());

        // TODO: test actual data once implementation is done.
        assert!(iterator.next().await?.is_empty());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn notify_interaction() -> Result<(), Error> {
        let client = setup()?;
        // TODO: test actual data once implementation is done.
        assert!(client.notify_interaction("id", InteractionType::Selected).is_ok());
        Ok(())
    }
}
