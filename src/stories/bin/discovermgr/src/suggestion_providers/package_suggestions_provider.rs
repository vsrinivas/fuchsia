// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        models::{AddModInfo, DisplayInfo, Suggestion},
        story_context_store::ContextEntity,
        suggestions_manager::SearchSuggestionsProvider,
    },
    anyhow::Error,
    fidl_fuchsia_sys_index::{ComponentIndexMarker, ComponentIndexProxy},
    fuchsia_component::client::{launch, launcher, App},
    fuchsia_syslog::macros::*,
    futures::future::LocalFutureObj,
    regex::Regex,
};

const COMPONENT_INDEX_URL: &str =
    "fuchsia-pkg://fuchsia.com/component_index#meta/component_index.cmx";

pub struct PackageSuggestionsProvider {}

impl PackageSuggestionsProvider {
    pub fn new() -> Self {
        PackageSuggestionsProvider {}
    }

    /// Starts and connects to the index service.
    fn get_index_service(&self) -> Result<(App, ComponentIndexProxy), Error> {
        let app = launch(&launcher()?, COMPONENT_INDEX_URL.to_string(), None)?;
        let service = app.connect_to_service::<ComponentIndexMarker>()?;
        Ok((app, service))
    }

    /// Generate a suggestion to open the a package given its url.
    /// (example url: fuchsia-pkg://fuchsia.com/package#meta/package.cmx)
    fn package_url_to_suggestion(url: String) -> Option<Suggestion> {
        let mut display_info = DisplayInfo::new();
        let re = Regex::new(r"fuchsia-pkg://fuchsia.com/(?P<name>.+)#meta/.+").unwrap();
        let caps = re.captures(&url)?;
        display_info.title = Some(format!("open {}", &caps["name"]));
        Some(Suggestion::new(
            AddModInfo::new_raw(&url, None /* story_name */, None /* mod_name */),
            display_info,
        ))
    }
}

// TODO: move this provider to Ermine once we support registering external providers.
// The provider should generate a suggestion to open the terminal
// "Run package.cmx on terminal" executing `run fuchsia-pkg://fuchsia.com/package#package.cmx`
impl SearchSuggestionsProvider for PackageSuggestionsProvider {
    fn request<'a>(
        &'a self,
        query: &'a str,
        _context: &'a Vec<&'a ContextEntity>,
    ) -> LocalFutureObj<'a, Result<Vec<Suggestion>, Error>> {
        LocalFutureObj::new(Box::new(async move {
            if query.is_empty() {
                return Ok(vec![]);
            }
            let (_app, index_service) = self.get_index_service().map_err(|e| {
                fx_log_err!("Failed to connect to index service");
                e
            })?;
            let index_response = index_service.fuzzy_search(query).await.map_err(|e| {
                fx_log_err!("Fuzzy search error from component index: {:?}", e);
                e
            })?;
            index_response
                .map(|results| {
                    results
                        .into_iter()
                        .filter_map(&PackageSuggestionsProvider::package_url_to_suggestion)
                        .collect::<Vec<Suggestion>>()
                })
                .or_else(|e| {
                    fx_log_err!("Fuzzy search error from component index: {:?}", e);
                    Ok(vec![])
                })
        }))
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::models::SuggestedAction, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn test_request() -> Result<(), Error> {
        let package_suggestions_provider = PackageSuggestionsProvider::new();
        let context = vec![];
        let results = package_suggestions_provider.request("discovermgr", &context).await?;
        assert_eq!(results.len(), 2);
        for result in results.into_iter() {
            let title = result.display_info().title.as_ref().unwrap();
            match result.action() {
                SuggestedAction::AddMod(action) => {
                    let handler = action.intent.handler.as_ref().unwrap();
                    if title == "open discovermgr" {
                        assert_eq!(
                            handler,
                            "fuchsia-pkg://fuchsia.com/discovermgr#meta/discovermgr.cmx"
                        );
                    } else if title == "open discovermgr_tests" {
                        assert_eq!(
                    handler,
                    "fuchsia-pkg://fuchsia.com/discovermgr_tests#meta/discovermgr_bin_test.cmx"
                );
                    } else {
                        assert!(false, format!("unexpected result {:?}", result));
                    }
                }
                SuggestedAction::RestoreStory(_) => {
                    assert!(false);
                }
            }
        }
        Ok(())
    }

    #[test]
    fn test_package_url_to_suggestion() {
        let url = "fuchsia-pkg://fuchsia.com/my_component#meta/my_component.cmx";
        let suggestion =
            PackageSuggestionsProvider::package_url_to_suggestion(url.to_string()).unwrap();
        match suggestion.action() {
            SuggestedAction::AddMod(action) => {
                assert_eq!(action.intent.handler, Some(url.to_string()));
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }
        assert_eq!(suggestion.display_info().title, Some("open my_component".to_string()));
    }

    #[test]
    fn test_package_url_to_suggestion_malformed_url() {
        let url = "fuchsia-pkg://fuchsia/my_component.cmx".to_string();
        assert!(PackageSuggestionsProvider::package_url_to_suggestion(url).is_none());
    }
}
