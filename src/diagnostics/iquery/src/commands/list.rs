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
    diagnostics_data::{Lifecycle, LifecycleData, LifecycleType},
    serde::{Serialize, Serializer},
    std::{cmp::Ordering, collections::BTreeSet},
};

#[derive(Debug, Eq, PartialEq, PartialOrd, Ord, Serialize)]
pub struct MonikerWithUrl {
    pub moniker: String,
    pub component_url: String,
}

#[derive(Debug, Eq, PartialEq, Ord)]
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

pub fn components_from_lifecycle_data(lifecycle_data: Vec<LifecycleData>) -> Vec<ListResponseItem> {
    let mut result = vec![];
    for value in lifecycle_data {
        // TODO(fxbug.dev/55118): when we can filter on metadata on a StreamDiagnostics
        // request, this manual filtering won't be necessary.
        if value.metadata.lifecycle_event_type == LifecycleType::DiagnosticsReady {
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
    /// the path from where to get the ArchiveAccessor connection. If the given path is a
    /// directory, the command will look for a `fuchsia.diagnostics.ArchiveAccessor` service file.
    /// If the given path is a service file, the command will attempt to connect to it as an
    /// ArchiveAccessor.
    pub accessor_path: Option<String>,
}

#[async_trait]
impl Command for ListCommand {
    type Result = Vec<ListResponseItem>;

    async fn execute<P: DiagnosticsProvider>(&self, provider: &P) -> Result<Self::Result, Error> {
        let lifecycle = provider.snapshot::<Lifecycle>(&self.accessor_path, &[]).await?;
        let components = components_from_lifecycle_data(lifecycle);
        let results =
            list_response_items_from_components(&self.manifest, self.with_url, components);
        Ok(results)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use diagnostics_data::Timestamp;

    #[test]
    fn components_from_lifecycle_data_uses_diagnostics_ready() {
        let lifecycle_data = vec![
            LifecycleData::for_lifecycle_event(
                "some_moniker",
                LifecycleType::Started,
                None,
                "fake-url",
                Timestamp::from(123456789800i64),
                vec![],
            ),
            LifecycleData::for_lifecycle_event(
                "other_moniker",
                LifecycleType::Started,
                None,
                "other-fake-url",
                Timestamp::from(123456789900i64),
                vec![],
            ),
            LifecycleData::for_lifecycle_event(
                "some_moniker",
                LifecycleType::DiagnosticsReady,
                None,
                "fake-url",
                Timestamp::from(123456789910i64),
                vec![],
            ),
            LifecycleData::for_lifecycle_event(
                "different_moniker",
                LifecycleType::Running,
                None,
                "different-fake-url",
                Timestamp::from(123456790900i64),
                vec![],
            ),
            LifecycleData::for_lifecycle_event(
                "different_moniker",
                LifecycleType::DiagnosticsReady,
                None,
                "different-fake-url",
                Timestamp::from(123456790990i64),
                vec![],
            ),
        ];

        let components = components_from_lifecycle_data(lifecycle_data);

        assert_eq!(components.len(), 2);
    }
}
