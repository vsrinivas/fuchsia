// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::types::*,
        types::{Error, ToText},
    },
    anyhow::Context,
    argh::FromArgs,
    async_trait::async_trait,
    diagnostics_data::{LifecycleSchema, LifecycleType},
    fidl_fuchsia_diagnostics::{
        ArchiveAccessorMarker, BatchIteratorMarker, ClientSelectorConfiguration, DataType, Format,
        FormattedContent, StreamMode, StreamParameters,
    },
    fuchsia_component::client,
    serde::{Serialize, Serializer},
    std::{cmp::Ordering, collections::BTreeSet},
};

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
        let results = get_ready_components()
            .await?
            .into_iter()
            .filter(|result| match &self.manifest {
                None => true,
                Some(manifest) => result.component_url.contains(manifest),
            })
            .map(|result| {
                if self.with_url {
                    ListResponseItem::MonikerWithUrl(result)
                } else {
                    ListResponseItem::Moniker(result.moniker)
                }
            })
            // Collect as btreeset to sort and remove potential duplicates.
            .collect::<BTreeSet<_>>();
        Ok(results.into_iter().collect::<Vec<_>>())
    }
}

async fn get_ready_components() -> Result<Vec<MonikerWithUrl>, Error> {
    let values = get_lifecycle_response().await.map_err(|e| Error::Fetch(e))?;
    let mut result = vec![];
    for value in values {
        // TODO(fxbug.dev/55118): when we can filter on metadata on a StreamDiagnostics
        // request, this manual filtering won't be necessary.
        if value.metadata.lifecycle_event_type == LifecycleType::DiagnosticsReady {
            result.push(MonikerWithUrl {
                moniker: value.moniker,
                component_url: value.metadata.component_url.clone(),
            });
        }
    }
    Ok(result)
}

async fn get_lifecycle_response() -> Result<Vec<LifecycleSchema>, anyhow::Error> {
    // TODO(fxbug.dev/55138): refactor into DiagnosticsDataFetcher
    let archive =
        client::connect_to_service::<ArchiveAccessorMarker>().context("connect to archive")?;
    let mut stream_parameters = StreamParameters::empty();
    stream_parameters.stream_mode = Some(StreamMode::Snapshot);
    stream_parameters.data_type = Some(DataType::Lifecycle);
    stream_parameters.format = Some(Format::Json);
    stream_parameters.client_selector_configuration =
        Some(ClientSelectorConfiguration::SelectAll(true));
    let (iterator, server_end) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()
        .context("failed to create iterator proxy")?;
    archive.stream_diagnostics(stream_parameters, server_end).context("get BatchIterator").unwrap();

    let mut result = Vec::new();
    loop {
        let next_batch = iterator.get_next().await.context("failed to get batch")?.unwrap();
        if next_batch.is_empty() {
            break;
        }
        for formatted_content in next_batch {
            match formatted_content {
                FormattedContent::Json(data) => {
                    let mut buf = vec![0; data.size as usize];
                    data.vmo.read(&mut buf, 0).context("reading vmo")?;
                    let hierarchy_json = std::str::from_utf8(&buf).unwrap();
                    let output: LifecycleSchema =
                        serde_json::from_str(&hierarchy_json).context("valid json")?;
                    result.push(output);
                }
                _ => unreachable!("JSON was requested, no other data type should be received"),
            }
        }
    }

    Ok(result)
}
