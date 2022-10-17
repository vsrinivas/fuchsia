// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::constants::{IQUERY_TIMEOUT, IQUERY_TIMEOUT_SECS},
        commands::target::connect_realm_protocols,
        commands::types::*,
        commands::utils::{
            get_instance_infos, prepend_leading_moniker, strip_leading_relative_moniker,
        },
        types::{Error, ToText},
    },
    anyhow::anyhow,
    argh::FromArgs,
    async_trait::async_trait,
    fidl_fuchsia_io as fio, fuchsia_fs,
    futures::StreamExt,
    itertools::Itertools,
    lazy_static::lazy_static,
    regex::Regex,
    serde::Serialize,
    std::cmp::Ordering,
};

lazy_static! {
    static ref CURRENT_DIR: Vec<String> = vec![".".to_string()];
    static ref MATCHING_REGEX: &'static str =
        r"^((.*\.inspect)|(fuchsia.inspect.Tree)|(fuchsia.inspect.deprecated.Inspect))$";
}

#[derive(Debug, Serialize, PartialEq, Eq)]
pub struct ListFilesResultItem {
    moniker: String,
    files: Vec<String>,
}

impl PartialOrd for ListFilesResultItem {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for ListFilesResultItem {
    fn cmp(&self, other: &Self) -> Ordering {
        self.moniker.cmp(&other.moniker)
    }
}

impl ToText for ListFilesResultItem {
    fn to_text(self) -> String {
        format!("{}\n  {}", &self.moniker, &self.files.join("\n  "))
    }
}

impl ToText for Vec<ListFilesResultItem> {
    fn to_text(self) -> String {
        self.into_iter().map(|e| e.to_text()).join("\n")
    }
}

/// Lists all inspect files (*inspect vmo files, fuchsia.inspect.Tree and
/// fuchsia.inspect.deprecated.Inspect) under the provided paths. If no monikers are provided, it'll
/// list all the inspect files for all components.
#[derive(Default, FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list-files")]
pub struct ListFilesCommand {
    #[argh(positional)]
    /// monikers to query on.
    pub monikers: Vec<String>,
}

async fn recursive_list_inspect_files(proxy: fio::DirectoryProxy) -> Vec<String> {
    let expected_accessor_re = Regex::new(&MATCHING_REGEX).unwrap();
    fuchsia_fs::directory::readdir_recursive(&proxy, Some(IQUERY_TIMEOUT.into()))
        .collect::<Vec<_>>()
        .await
        .into_iter()
        .filter_map(|entry| match entry {
            Ok(ref dir_entry) => {
                if (dir_entry.kind == fuchsia_fs::directory::DirentKind::File
                    || dir_entry.kind == fuchsia_fs::directory::DirentKind::Service)
                    && expected_accessor_re.is_match(&dir_entry.name)
                {
                    Some(String::from(&dir_entry.name))
                } else {
                    None
                }
            }
            Err(fuchsia_fs::directory::Error::Timeout) => {
                eprintln!(
                    "Warning: Read directory timed out after {} second(s)",
                    IQUERY_TIMEOUT_SECS,
                );
                None
            }
            Err(_) => None,
        })
        .collect::<Vec<_>>()
}

#[async_trait]
impl Command for ListFilesCommand {
    type Result = Vec<ListFilesResultItem>;

    async fn execute<P: DiagnosticsProvider>(&self, _provider: &P) -> Result<Self::Result, Error> {
        let (realm_query_proxy, mut realm_explorer_proxy) = connect_realm_protocols().await?;

        let monikers = if self.monikers.is_empty() {
            get_instance_infos(&mut realm_explorer_proxy)
                .await?
                .iter()
                .map(|e| e.moniker.to_owned())
                .collect::<Vec<_>>()
        } else {
            let mut processed = vec![];
            for moniker in self.monikers.iter() {
                processed.push(prepend_leading_moniker(moniker).await);
            }
            processed
        };

        let mut output_vec = vec![];

        for moniker in &monikers {
            let (_, res_info) = realm_query_proxy
                .get_instance_info(moniker)
                .await
                .map_err(|e| Error::ConnectingTo("RealmQuery".to_owned(), e))?
                .map_err(|e| {
                    Error::CommunicatingWith("RealmQuery".to_owned(), anyhow!("{:?}", e))
                })?;

            if let Some(resolved_state) = res_info {
                let execution_state = match resolved_state.execution {
                    Some(state) => state,
                    _ => continue,
                };

                let out_dir = match execution_state.out_dir {
                    Some(out_dir) => out_dir,
                    _ => continue,
                };

                let out_dir_proxy = match out_dir.into_proxy() {
                    Ok(p) => p,
                    Err(_) => continue,
                };

                // `fuchsia_fs::directory::open_directory` could block forever when the directory
                // it is trying to open does not exist.
                // TODO(https://fxbug.dev/110964): use `open_directory` hang bug is fixed.
                let diagnostics_dir_proxy = match fuchsia_fs::directory::open_directory_no_describe(
                    &out_dir_proxy,
                    "diagnostics",
                    fuchsia_fs::OpenFlags::RIGHT_READABLE,
                ) {
                    Ok(p) => p,
                    Err(_) => continue,
                };

                let mut files = recursive_list_inspect_files(diagnostics_dir_proxy).await;
                files.sort();

                if files.len() > 0 {
                    output_vec.push(ListFilesResultItem {
                        moniker: String::from(strip_leading_relative_moniker(moniker).await),
                        files,
                    })
                }
            }
        }

        output_vec.sort();
        Ok(output_vec)
    }
}
