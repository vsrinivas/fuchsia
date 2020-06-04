// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::*, utils},
        types::{Error, ToText},
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

impl ToText for Vec<ListResponseItem> {
    fn to_text(self) -> String {
        self.into_iter()
            .map(|item| match item {
                ListResponseItem::Component(string) => string,
            })
            .collect::<Vec<_>>()
            .join("\n")
    }
}

/// Lists all components (relative to the scope where the archivist receives events from) of
/// components that expose inspect.
/// For v1: this is the realm path plus the realm name
/// For v2: this is the moniker without the instances ids
#[derive(Default, FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list")]
pub struct ListCommand {
    // TODO(fxbug.dev/45458): uncomment when implemented.
// #[argh(option)]
// /// the name of the manifest file that we are interested in. If this is provided, the output
// /// will only contain monikers for components whose url contains the provided name.
// pub manifest_name: Option<String>,

// #[argh(switch)]
// /// also print the URL of the component.
// pub with_url: bool,
}

#[async_trait]
impl Command for ListCommand {
    type Result = Vec<ListResponseItem>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        // TODO(fxbug.dev/45458): support filtering by manifest name
        // TODO(fxbug.dev/45458): support including the url in the response
        // TODO(fxbug.dev/51165): once the archive exposes lifecycle we don't need to query all the
        // inspect data. We can just query a snapshot of all running components with inspect data.
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
