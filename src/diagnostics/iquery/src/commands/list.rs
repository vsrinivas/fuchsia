// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::*, utils},
        types::Error,
    },
    argh::FromArgs,
    async_trait::async_trait,
    fuchsia_inspect::testing::InspectDataFetcher,
    serde::{Serialize, Serializer},
    std::{cmp::Ordering, collections::BTreeSet},
};

#[derive(Debug, Eq, PartialEq, Ord)]
pub enum ListResponseItem {
    Component(String),
}

impl Serialize for ListResponseItem {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        match self {
            Self::Component(string) => serializer.serialize_str(&string),
        }
    }
}

impl PartialOrd for ListResponseItem {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        match (self, other) {
            (
                ListResponseItem::Component(component),
                ListResponseItem::Component(other_component),
            ) => component.partial_cmp(other_component),
        }
    }
}

/// Lists all components (relative to the scope where the archivist receives events from) of
/// components that expose inspect.
/// For v1: this is the realm path plus the realm name
/// For v2: this is the moniker without the instances ids
#[derive(Default, FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list")]
pub struct ListCommand {
    #[argh(switch)]
    /// print v1 /hub entries that contain inspect.
    pub hub: bool,

    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest_name: Option<String>,

    #[argh(switch)]
    /// also print the URL of the component.
    pub with_url: bool,
}

#[async_trait]
impl Command for ListCommand {
    type Result = Vec<ListResponseItem>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        // TODO(fxbug.dev/45458): support listing hub entries
        // TODO(fxbug.dev/45458): support filtering by manifest name
        // TODO(fxbug.dev/45458): support including the url in the response
        // TODO(fxbug.dev/51165): once the archive exposes lifecycle we don't need to query all the
        // inspect data. We can just query a snapshot of all running components.
        let fetcher = InspectDataFetcher::new();
        let mut results = fetcher.get_raw_json().await.map_err(|e| Error::Fetch(e))?;
        let mut results = results
            .as_array_mut()
            .ok_or(Error::ArchiveInvalidJson)?
            .into_iter()
            .map(|result| {
                let component = utils::get_moniker_from_result(&result)?;
                Ok(ListResponseItem::Component(component))
            })
            .collect::<Result<Vec<_>, Error>>()?;
        // We need to remove duplicates, given that some components might expose multiple inspect
        // sources (for example multiple vmo files). We can differentiate through them using
        // selectors but the component selector remains the same.
        results = results.drain(..).collect::<BTreeSet<_>>().into_iter().collect::<Vec<_>>();
        Ok(results)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::testing, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn test_list() {
        let (_env, _app) =
            testing::start_basic_component("list-test").await.expect("create comp 1");
        let (_env2, _app2) =
            testing::start_basic_component("list-test2").await.expect("create comp 2");
        let mut result;
        loop {
            let command = ListCommand::default();
            result = command.execute().await.expect("successful execution");
            // We expect 3 components: the two we started and the observer
            // We might get less than 3 in the first try. Keep retrying otherwise we'll flake.
            // Usually only another retry is needed if all the data wasn't present on the first
            // attempt.
            if result.len() == 3 {
                break;
            }
        }
        testing::assert_result(result, include_str!("../../test_data/list_expected.json"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_no_duplicates() {
        let (_env, _app) =
            testing::start_test_component("list-dup-test").await.expect("create comp 1");
        let mut result;
        loop {
            let command = ListCommand::default();
            result = command.execute().await.expect("successful execution");
            // We expect 2: the observer and only 1 for the component even when it exposes 3
            // sources. Using a >= comparison to fail the test rather than hang it if a regression
            // is introduced.
            // We might get less than 1 in the first try. Keep retrying otherwise we'll flake.
            // Usually only another retry is needed if all the data wasn't present on the first
            // attempt.
            if result.len() == 2 {
                break;
            }
        }
        testing::assert_result(result, r#"["list-dup-test/test_component.cmx","observer.cmx"]"#);
    }
}
