// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::types::*,
        types::{Error, ToText},
    },
    argh::FromArgs,
    async_trait::async_trait,
    diagnostics_data::{Inspect, InspectData},
    serde::{Serialize, Serializer},
    std::{cmp::Ordering, collections::BTreeSet},
};

#[derive(Debug, Eq, PartialEq, PartialOrd, Ord, Serialize)]
pub struct MonikerWithUrl {
    pub moniker: String,
    pub component_url: String,
}

#[derive(Debug, Eq, PartialEq)]
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

impl Ord for ListResponseItem {
    fn cmp(&self, other: &Self) -> Ordering {
        self.partial_cmp(other).unwrap()
    }
}

impl PartialOrd for ListResponseItem {
    // Compare based on the moniker only. To enable sorting using the moniker only.
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        match (self, other) {
            (ListResponseItem::Moniker(moniker), ListResponseItem::Moniker(other_moniker))
            | (
                ListResponseItem::MonikerWithUrl(MonikerWithUrl { moniker, .. }),
                ListResponseItem::MonikerWithUrl(MonikerWithUrl { moniker: other_moniker, .. }),
            ) => moniker.partial_cmp(other_moniker),
            _ => unreachable!("all lists must contain variants of the same type"),
        }
    }
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

fn components_from_inspect_data(inspect_data: Vec<InspectData>) -> Vec<ListResponseItem> {
    let mut result = vec![];
    for value in inspect_data {
        match value.metadata.component_url {
            Some(ref url) => {
                result.push(ListResponseItem::MonikerWithUrl(MonikerWithUrl {
                    moniker: value.moniker,
                    component_url: url.clone(),
                }));
            }
            None => {
                result.push(ListResponseItem::Moniker(value.moniker));
            }
        }
    }
    result
}

pub fn list_response_items_from_components(
    manifest: &Option<String>,
    with_url: bool,
    components: Vec<ListResponseItem>,
) -> Vec<ListResponseItem> {
    components
        .into_iter()
        .filter(|result| match manifest {
            None => true,
            Some(manifest) => match result {
                ListResponseItem::Moniker(_) => true,
                ListResponseItem::MonikerWithUrl(url) => url.component_url.contains(manifest),
            },
        })
        .map(|result| {
            if with_url {
                result
            } else {
                match result {
                    ListResponseItem::Moniker(_) => result,
                    ListResponseItem::MonikerWithUrl(val) => ListResponseItem::Moniker(val.moniker),
                }
            }
        })
        // Collect as btreeset to sort and remove potential duplicates.
        .collect::<BTreeSet<_>>()
        .into_iter()
        .collect::<Vec<_>>()
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

    #[argh(option)]
    /// A selector specifying what `fuchsia.diagnostics.ArchiveAccessor` to connect to.
    /// The selector will be in the form of:
    /// <moniker>:<directory>:fuchsia.diagnostics.ArchiveAccessorName
    ///
    /// Typically this is the output of `iquery list-accessors`.
    ///
    /// For example: `bootstrap/archivist:expose:fuchsia.diagnostics.FeedbackArchiveAccessor`
    /// means that the command will connect to the `FeedbackArchiveAccecssor`
    /// exposed by `bootstrap/archivist`.
    pub accessor: Option<String>,
}

#[async_trait]
impl Command for ListCommand {
    type Result = Vec<ListResponseItem>;

    async fn execute<P: DiagnosticsProvider>(&self, provider: &P) -> Result<Self::Result, Error> {
        let inspect = provider.snapshot::<Inspect>(&self.accessor, &[]).await?;
        let components = components_from_inspect_data(inspect);
        let results =
            list_response_items_from_components(&self.manifest, self.with_url, components);
        Ok(results)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use diagnostics_data::Timestamp;

    #[fuchsia::test]
    fn components_from_inspect_data_uses_diagnostics_ready() {
        let inspect_data = vec![
            InspectData::for_inspect(
                "some_moniker",
                /*inspect_hierarchy=*/ None,
                Timestamp::from(123456789800i64),
                "fake-url",
                "fake-file",
                vec![],
            ),
            InspectData::for_inspect(
                "other_moniker",
                /*inspect_hierarchy=*/ None,
                Timestamp::from(123456789900i64),
                "other-fake-url",
                "fake-file",
                vec![],
            ),
            InspectData::for_inspect(
                "some_moniker",
                /*inspect_hierarchy=*/ None,
                Timestamp::from(123456789910i64),
                "fake-url",
                "fake-file",
                vec![],
            ),
            InspectData::for_inspect(
                "different_moniker",
                /*inspect_hierarchy=*/ None,
                Timestamp::from(123456790990i64),
                "different-fake-url",
                "fake-file",
                vec![],
            ),
        ];

        let components = components_from_inspect_data(inspect_data);

        assert_eq!(components.len(), 4);
    }
}
