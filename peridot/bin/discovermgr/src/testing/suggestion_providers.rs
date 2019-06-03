// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    crate::{
        models::{AddMod, DisplayInfo, Intent, Suggestion},
        story_context_store::ContextEntity,
        suggestions_manager::SearchSuggestionsProvider,
    },
    failure::Error,
    futures::future::LocalFutureObj,
};

pub struct TestSuggestionsProvider {
    suggestions: Vec<Suggestion>,
}

impl TestSuggestionsProvider {
    pub fn new() -> Self {
        TestSuggestionsProvider {
            suggestions: vec![
                suggestion!(
                    action = "PLAY_MUSIC",
                    title = "Listen to Garnet",
                    parameters = [(name = "artist", entity_reference = "abcdefgh")]
                ),
                suggestion!(
                    action = "PLAY_MUSIC",
                    title = "Listen to Peridot",
                    parameters = [(name = "artist", entity_reference = "123456")]
                ),
                suggestion!(
                    action = "SEE_CONCERTS",
                    title = "See concerts for Garnet",
                    parameters = [(name = "artist", entity_reference = "abcdefgh")]
                ),
            ],
        }
    }
}

impl SearchSuggestionsProvider for TestSuggestionsProvider {
    fn request<'a>(
        &'a self,
        _query: &'a str,
        _context: &'a Vec<ContextEntity>,
    ) -> LocalFutureObj<'a, Result<Vec<Suggestion>, Error>> {
        LocalFutureObj::new(Box::new(async move { Ok(self.suggestions.clone()) }))
    }
}
