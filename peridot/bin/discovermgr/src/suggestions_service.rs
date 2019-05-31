// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::story_context_store::{ContextEntity, StoryContextStore},
    crate::suggestions_manager::SuggestionsManager,
    failure::{Error, ResultExt},
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
    context_store: Arc<Mutex<StoryContextStore>>,
    suggestions_manager: Arc<Mutex<SuggestionsManager>>,
}

impl SuggestionsService {
    pub fn new(
        context_store: Arc<Mutex<StoryContextStore>>,
        suggestions_manager: Arc<Mutex<SuggestionsManager>>,
    ) -> Self {
        SuggestionsService { context_store, suggestions_manager }
    }

    pub async fn handle_client(
        &mut self,
        mut stream: SuggestionsRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            await!(stream.try_next()).context("error running discover context")?
        {
            match request {
                SuggestionsRequest::GetSuggestions { query, iterator, .. } => {
                    let stream = iterator.into_stream()?;
                    self.serve_suggestions(query, stream);
                }
                SuggestionsRequest::NotifyInteraction { suggestion_id, interaction, .. } => {
                    match interaction {
                        InteractionType::Selected => {
                            self.suggestions_manager.lock().execute(suggestion_id)
                        }
                    }
                }
            }
        }
        Ok(())
    }

    pub fn serve_suggestions(&self, query: String, mut stream: SuggestionsIteratorRequestStream) {
        let context = self.context_store.lock().current().cloned().collect::<Vec<ContextEntity>>();
        let suggestions = self
            .suggestions_manager
            .lock()
            .get_suggestions(query, context)
            .map(|s| s.clone().into())
            .collect::<Vec<Suggestion>>();
        fasync::spawn(
            async move {
                let mut iter = suggestions.into_iter();
                while let Some(request) = await!(stream.try_next())? {
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
        fidl_fuchsia_app_discover::{
            SuggestionsIteratorMarker, SuggestionsMarker, SuggestionsProxy,
        },
        fidl_fuchsia_modular::PuppetMasterMarker,
    };

    fn setup() -> Result<SuggestionsProxy, Error> {
        let (puppet_master, _) = fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>()?;

        let context_store = Arc::new(Mutex::new(StoryContextStore::new()));
        let suggestions_manager = Arc::new(Mutex::new(SuggestionsManager::new(puppet_master)));

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<SuggestionsMarker>()?;
        fasync::spawn(
            async move {
                let mut service = SuggestionsService::new(context_store, suggestions_manager);
                await!(service.handle_client(request_stream))
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
        assert!(await!(iterator.next())?.is_empty());

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
