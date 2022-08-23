// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    async_trait::async_trait,
    diagnostics_data::Data,
    errors::ffx_error,
    ffx_writer::Writer,
    fidl::{endpoints::create_proxy, AsyncSocket},
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorMarker, BridgeStreamParameters, DiagnosticsData, RemoteControlProxy,
        RemoteDiagnosticsBridgeProxy,
    },
    fidl_fuchsia_diagnostics::{
        ClientSelectorConfiguration::{SelectAll, Selectors},
        SelectorArgument,
    },
    fidl_fuchsia_io as fio,
    fuchsia_async::Duration,
    fuchsia_fs,
    futures::future::join_all,
    futures::AsyncReadExt as _,
    futures::StreamExt,
    iquery::{
        commands::DiagnosticsProvider,
        types::{Error, ToText},
    },
    lazy_static::lazy_static,
    regex::Regex,
    std::{collections::BTreeSet, io::Write, path::PathBuf},
};

lazy_static! {
    static ref EXPECTED_FILE_RE: &'static str = r"fuchsia\.diagnostics\..*ArchiveAccessor$";
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
        writer
            .write_fmt(format_args!("{}", result.to_text()))
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
        accessor_path: &Option<String>,
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
        let params = BridgeStreamParameters {
            stream_mode: Some(fidl_fuchsia_diagnostics::StreamMode::Snapshot),
            data_type: Some(D::DATA_TYPE),
            accessor_path: accessor_path.clone(),
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
        let paths = if paths.is_empty() { vec!["".to_string()] } else { paths.clone() };

        let mut result = BTreeSet::new();
        let path_futs = paths.iter().map(|path| all_accessors(&self.rcs_proxy, path));
        for paths_result in join_all(path_futs).await {
            let paths =
                paths_result.map_err(|e| Error::ListAccessors("querying path".into(), e.into()))?;
            result.extend(paths.into_iter());
        }
        Ok(result.into_iter().collect::<Vec<_>>())
    }
}

async fn all_accessors(
    rcs_proxy: &RemoteControlProxy,
    root: impl AsRef<str>,
) -> Result<Vec<String>, Error> {
    let (hub_root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .map_err(|e| Error::IOError("create directory marker proxy".into(), e.into()))?;
    rcs_proxy
        .open_hub(dir_server)
        .await
        .map_err(|e| Error::ListAccessors("talking to RemoteControlProxy".into(), e.into()))?
        .map_err(|e| Error::ListAccessors("open hub".into(), anyhow!("{:?}", e)))?;
    let dir_proxy = if root.as_ref().is_empty() {
        hub_root
    } else {
        fuchsia_fs::open_directory(
            &hub_root,
            std::path::Path::new(root.as_ref()),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .map_err(|e| Error::IOError(format!("Open dir {}", root.as_ref()), e.into()))?
    };
    let expected_file_re = Regex::new(&EXPECTED_FILE_RE).unwrap();

    let paths = fuchsia_fs::directory::readdir_recursive(
        &dir_proxy,
        Some(Duration::from_secs(*READDIR_TIMEOUT_SECONDS)),
    )
    .filter_map(|result| async {
        match result {
            Err(err) => {
                eprintln!("{}", err);
                None
            }
            Ok(entry) => {
                if expected_file_re.is_match(&entry.name) {
                    let mut path = PathBuf::from("/hub-v2");
                    path.push(root.as_ref());
                    path.push(&entry.name);
                    Some(path.to_string_lossy().to_string())
                } else {
                    None
                }
            }
        }
    })
    .collect::<Vec<String>>()
    .await;
    Ok(paths)
}
