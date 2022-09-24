// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    async_trait::async_trait,
    diagnostics_data::Data,
    errors::ffx_error,
    ffx_writer::Writer,
    fidl,
    fidl::{endpoints::create_proxy, AsyncSocket},
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorMarker, BridgeStreamParameters, DiagnosticsData, RemoteControlProxy,
        RemoteDiagnosticsBridgeProxy,
    },
    fidl_fuchsia_diagnostics::{
        ClientSelectorConfiguration::{SelectAll, Selectors},
        SelectorArgument,
    },
    fidl_fuchsia_sys2 as fsys2,
    futures::AsyncReadExt as _,
    iquery::{
        commands::{get_accessor_selectors, DiagnosticsProvider},
        types::{Error, ToText},
    },
    lazy_static::lazy_static,
    selectors,
    std::io::Write,
};

lazy_static! {
    static ref READDIR_TIMEOUT_SECONDS: u64 = 15;
}

pub async fn run_command(
    rcs_proxy: RemoteControlProxy,
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    cmd: impl iquery::commands::Command,
    mut writer: Writer,
) -> anyhow::Result<()> {
    let provider = DiagnosticsBridgeProvider::new(diagnostics_proxy, rcs_proxy);
    let result = cmd.execute(&provider).await.map_err(|e| anyhow!(ffx_error!("{}", e)))?;
    if writer.is_machine() {
        writer.machine(&result).context("Unable to write structured output to destination.")
    } else {
        // Ensure trailing newline.
        writer
            .write_fmt(format_args!("{}\n", result.to_text()))
            .context("Unable to write to destination.")
    }
}

pub struct DiagnosticsBridgeProvider {
    diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
    rcs_proxy: RemoteControlProxy,
}

impl DiagnosticsBridgeProvider {
    pub fn new(
        diagnostics_proxy: RemoteDiagnosticsBridgeProxy,
        rcs_proxy: RemoteControlProxy,
    ) -> Self {
        Self { diagnostics_proxy, rcs_proxy }
    }

    pub async fn snapshot_diagnostics_data<D>(
        &self,
        accessor: &Option<String>,
        selectors: &[String],
    ) -> Result<Vec<Data<D>>, Error>
    where
        D: diagnostics_data::DiagnosticsData,
    {
        let selectors = if selectors.is_empty() {
            SelectAll(true)
        } else {
            Selectors(selectors.iter().cloned().map(|s| SelectorArgument::RawSelector(s)).collect())
        };

        let accessor_selector = match accessor {
            Some(ref s) => {
                Some(selectors::parse_selector::<selectors::VerboseError>(s).map_err(|e| {
                    Error::ParseSelector("unable to parse selector".to_owned(), anyhow!("{:?}", e))
                })?)
            }
            None => None,
        };

        let params = BridgeStreamParameters {
            stream_mode: Some(fidl_fuchsia_diagnostics::StreamMode::Snapshot),
            data_type: Some(D::DATA_TYPE),
            accessor: accessor_selector,
            client_selector_configuration: Some(selectors),
            ..BridgeStreamParameters::EMPTY
        };

        let (client, server) = create_proxy::<ArchiveIteratorMarker>()
            .context("failed to create endpoints")
            .map_err(Error::ConnectToArchivist)?;

        let _ = self.diagnostics_proxy.stream_diagnostics(params, server).await.map_err(|s| {
            Error::IOError(
                "call diagnostics_proxy".into(),
                anyhow!("failure setting up diagnostics stream: {:?}", s),
            )
        })?;

        match client.get_next().await {
            Err(e) => {
                return Err(Error::IOError("get next".into(), e.into()));
            }
            Ok(result) => {
                // For style consistency we use a loop to process the event stream even though the
                // first event always terminates processing.
                #[allow(clippy::never_loop)]
                for entry in result
                    .map_err(|s| anyhow!("Iterator error: {:?}", s))
                    .map_err(|e| Error::IOError("iterate results".into(), e))?
                    .into_iter()
                {
                    match entry.diagnostics_data {
                        Some(DiagnosticsData::Inline(inline)) => {
                            let result = serde_json::from_str(&inline.data)
                                .map_err(Error::InvalidCommandResponse)?;
                            return Ok(result);
                        }
                        Some(DiagnosticsData::Socket(socket)) => {
                            let mut socket = AsyncSocket::from_socket(socket).map_err(|e| {
                                Error::IOError("create async socket".into(), e.into())
                            })?;
                            let mut result = Vec::new();
                            let _ = socket.read_to_end(&mut result).await.map_err(|e| {
                                Error::IOError("read snapshot from socket".into(), e.into())
                            })?;
                            let result = serde_json::from_slice(&result)
                                .map_err(Error::InvalidCommandResponse)?;
                            return Ok(result);
                        }
                        _ => {
                            return Err(Error::IOError(
                                "read diagnostics_data".into(),
                                anyhow!("unknown diagnostics data type"),
                            ))
                        }
                    }
                }
                return Ok(vec![]);
            }
        }
    }
}

#[async_trait]
impl DiagnosticsProvider for DiagnosticsBridgeProvider {
    async fn snapshot<D>(
        &self,
        accessor_path: &Option<String>,
        selectors: &[String],
    ) -> Result<Vec<Data<D>>, Error>
    where
        D: diagnostics_data::DiagnosticsData,
    {
        self.snapshot_diagnostics_data::<D>(accessor_path, selectors).await
    }

    async fn get_accessor_paths(&self, paths: &Vec<String>) -> Result<Vec<String>, Error> {
        let (mut query_proxy, mut explorer_proxy) = connect_realm_proxies(&self.rcs_proxy).await?;
        get_accessor_selectors(&mut explorer_proxy, &mut query_proxy, paths).await
    }
}

/// Connect to Root `RealmExplorer` and Root `RealmQuery` with the provided `RemoteControlProxy`.
async fn connect_realm_proxies(
    rcs_proxy: &RemoteControlProxy,
) -> Result<(fsys2::RealmQueryProxy, fsys2::RealmExplorerProxy), Error> {
    let (realm_explorer_proxy, realm_explorer_server_end) =
        fidl::endpoints::create_proxy::<fsys2::RealmExplorerMarker>()
            .map_err(|e| Error::IOError("create realm explorer proxy".into(), e.into()))?;
    // Connect to RootRealmExplorer.
    flatten_proxy_connection(
        rcs_proxy.root_realm_explorer(realm_explorer_server_end).await,
        "RootRealmExplorer",
    )
    .await?;

    // Connect RootRealmQuery.
    let (realm_query_proxy, realm_query_server_end) =
        fidl::endpoints::create_proxy::<fsys2::RealmQueryMarker>()
            .map_err(|e| Error::IOError("create realm query proxy".into(), e.into()))?;
    flatten_proxy_connection(
        rcs_proxy.root_realm_query(realm_query_server_end).await,
        "RootRealmQuery",
    )
    .await?;

    Ok((realm_query_proxy, realm_explorer_proxy))
}

/// Helper method to unwrap `RemoteControlProxy` creation.
async fn flatten_proxy_connection(
    entry_point: Result<Result<(), i32>, fidl::Error>,
    step_name: &str,
) -> Result<(), Error> {
    entry_point
        .map_err(|e| Error::IOError("talking to RemoteControlProxy".into(), e.into()))?
        .map_err(|e| Error::IOError(step_name.into(), anyhow!("{:?}", e)))
}
