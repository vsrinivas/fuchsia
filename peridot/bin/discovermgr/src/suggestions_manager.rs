// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{models::Suggestion, story_context_store::ContextEntity},
    fidl_fuchsia_modular::PuppetMasterProxy,
    std::collections::HashMap,
};

pub struct SuggestionsManager {
    suggestions: HashMap<String, Suggestion>,
    _puppet_master: PuppetMasterProxy,
}

impl SuggestionsManager {
    pub fn new(puppet_master: PuppetMasterProxy) -> Self {
        SuggestionsManager { suggestions: HashMap::new(), _puppet_master: puppet_master }
    }

    pub fn get_suggestions<'a>(
        &'a self,
        _query: String,
        _context: Vec<ContextEntity>,
    ) -> impl Iterator<Item = &'a Suggestion> {
        // TODO: this should look up in our mod index and generate suggestions
        // for mods that match the current context. It should store the generated
        // suggestions and return them. Keep track of the contributors that
        // caused each suggestion to be generated.
        self.suggestions.values()
    }

    pub fn execute(&self, _id: String) {
        // TODO: execute the intent associated with the given suggestion id. If
        // one of the contributors outputs changes, we should re-issue the intent.
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn get_suggestions() {
        // TODO: once the method is implemented.
    }

    #[test]
    fn execute() {
        // TODO: once the method is implemented.
    }
}
