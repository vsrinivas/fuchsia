// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::*, utils},
        text_formatter,
        types::{Error, ToText},
    },
    argh::FromArgs,
    async_trait::async_trait,
    derivative::Derivative,
    diagnostics_data::{Inspect, InspectData},
    itertools::Itertools,
    serde::Serialize,
    std::{cmp::Ordering, ops::Deref},
};

#[derive(Derivative, Serialize, PartialEq)]
#[derivative(Eq)]
pub struct ShowCommandResultItem(InspectData);

impl Deref for ShowCommandResultItem {
    type Target = InspectData;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl PartialOrd for ShowCommandResultItem {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for ShowCommandResultItem {
    fn cmp(&self, other: &Self) -> Ordering {
        self.moniker.cmp(&other.moniker)
    }
}

impl ToText for Vec<ShowCommandResultItem> {
    fn to_text(self) -> String {
        self.into_iter()
            .map(|item| text_formatter::format_schema(item.0))
            .collect::<Vec<_>>()
            .join("\n")
    }
}

/// Prints the inspect hierarchies that match the given selectors.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "show")]
pub struct ShowCommand {
    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest: Option<String>,

    #[argh(positional)]
    /// selectors for which the selectors should be queried. If no selectors are provided, inspect
    /// data for the whole system will be returned. If `--manifest` is provided then the selectors
    /// should be tree selectors, otherwise component selectors or full selectors.
    pub selectors: Vec<String>,

    #[argh(option)]
    /// the filename we are interested in. If this is provided, the output will only
    /// contain data from components which expose Inspect under the given file under
    /// their out/diagnostics directory.
    pub file: Option<String>,

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
    ///
    /// NOTE: if `<directory>` starts with `out`, then it means `out/svc` implicitly.
    pub accessor: Option<String>,
}

#[async_trait]
impl Command for ShowCommand {
    type Result = Vec<ShowCommandResultItem>;

    async fn execute<P: DiagnosticsProvider>(&self, provider: &P) -> Result<Self::Result, Error> {
        let selectors = utils::get_selectors_for_manifest(
            &self.manifest,
            &self.selectors,
            &self.accessor,
            provider,
        )
        .await?;
        let selectors = utils::expand_selectors(selectors)?;

        let accessor = match &self.accessor {
            Some(a) => {
                let elements: Vec<&str> = a.split(":").collect();
                if elements.len() != 3 {
                    return Err(Error::InvalidAccessor(a.to_owned()));
                }
                // Append "svc" to "out" if needed.
                let dir = elements[1];
                let dir = if dir == "out" || dir.starts_with("out/") {
                    let dir_elements = &dir.split("/").collect::<Vec<_>>();
                    ["out/svc"].iter().chain(dir_elements[1..].into_iter()).join("/")
                } else {
                    dir.to_owned()
                };
                Some([elements[0], &dir, elements[2]].join(":"))
            }
            _ => None,
        };

        let inspect_data_iter =
            provider.snapshot::<Inspect>(&accessor, &selectors).await?.into_iter();
        // Filter out by filename on the Inspect metadata.
        let filter_fn: Box<dyn Fn(&InspectData) -> bool> = match &self.file {
            Some(file) => {
                let file_clone = file.to_owned();
                Box::new(move |d: &InspectData| &d.metadata.filename == &file_clone)
            }
            _ => Box::new(|_d: &InspectData| true),
        };

        let mut results = inspect_data_iter
            .filter(filter_fn)
            .map(|mut d: InspectData| {
                if let Some(hierarchy) = &mut d.payload {
                    hierarchy.sort();
                }
                ShowCommandResultItem(d)
            })
            .collect::<Vec<_>>();

        results.sort();
        Ok(results)
    }
}
