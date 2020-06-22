// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::*, utils},
        constants,
        types::{Error, ToText},
    },
    argh::FromArgs,
    async_trait::async_trait,
    fuchsia_inspect::testing::InspectDataFetcher,
    serde::{Serialize, Serializer},
    std::collections::BTreeSet,
};

#[derive(Debug, Eq, PartialEq, PartialOrd, Ord)]
pub enum ListResponseItem {
    Moniker(String),
    MonikerWithUrl(MonikerWithUrl),
}

impl ListResponseItem {
    pub fn into_moniker(self) -> String {
        match self {
            Self::Moniker(moniker) => moniker,
            Self::MonikerWithUrl(MonikerWithUrl { moniker, .. }) => moniker,
        }
    }
}

#[derive(Debug, Eq, PartialEq, PartialOrd, Ord, Serialize)]
pub struct MonikerWithUrl {
    moniker: String,
    component_url: String,
}

impl Serialize for ListResponseItem {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        match self {
            Self::Moniker(string) => serializer.serialize_str(&string),
            Self::MonikerWithUrl(data) => data.serialize(serializer),
        }
    }
}

impl ToText for Vec<ListResponseItem> {
    fn to_text(self) -> String {
        self.into_iter()
            .map(|item| match item {
                ListResponseItem::Moniker(string) => string,
                ListResponseItem::MonikerWithUrl(MonikerWithUrl { component_url, moniker }) => {
                    format!("{}:\n  {}", moniker, component_url)
                }
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
    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest: Option<String>,

    #[argh(switch)]
    /// also print the URL of the component.
    pub with_url: bool,
}

#[async_trait]
impl Command for ListCommand {
    type Result = Vec<ListResponseItem>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        // TODO(fxbug.dev/51165): once the archive exposes lifecycle we don't need to query all the
        // inspect data. We can just query a snapshot of all running components with inspect data.
        let fetcher = InspectDataFetcher::new().with_timeout(*constants::IQUERY_TIMEOUT);
        let mut results = fetcher.get_raw_json().await.map_err(|e| Error::Fetch(e))?;
        let mut results = results
            .as_array_mut()
            .ok_or(Error::ArchiveInvalidJson)?
            .into_iter()
            .filter(|result| match &self.manifest {
                None => true,
                Some(manifest) => utils::get_url_from_result(&result)
                    .map(|url| url.contains(manifest))
                    .unwrap_or(false),
            })
            .map(|result| {
                let moniker = utils::get_moniker_from_result(&result)?;
                if self.with_url {
                    let component_url = utils::get_url_from_result(&result)?;
                    Ok(ListResponseItem::MonikerWithUrl(MonikerWithUrl { moniker, component_url }))
                } else {
                    Ok(ListResponseItem::Moniker(moniker))
                }
            })
            .collect::<Result<Vec<_>, Error>>()?;

        // We need to remove duplicates, given that some components might expose multiple inspect
        // sources (for example multiple vmo files). We can differentiate through them using
        // selectors but the component selector remains the same.
        results = results.drain(..).collect::<BTreeSet<_>>().into_iter().collect::<Vec<_>>();
        Ok(results)
    }
}
