// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::connect::*;
use anyhow::{anyhow, Context};
use fidl_fuchsia_boot::ArgumentsMarker;
use fidl_fuchsia_cobalt::{
    SoftwareDistributionInfo, Status as CobaltStatus, SystemDataUpdaterMarker,
    SystemDataUpdaterProxy,
};
use fidl_fuchsia_pkg::RepositoryManagerMarker;
use fidl_fuchsia_pkg_ext::RepositoryConfig;
use fidl_fuchsia_pkg_rewrite::EngineMarker;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use fuchsia_url::pkg_url::PkgUrl;
use fuchsia_zircon as zx;
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use serde_json;
use std::collections::{HashMap, HashSet};
use std::convert::TryInto;
use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::time::Duration;
use thiserror::Error;

static TARGET_CHANNEL: &'static str = "target_channel.json";

static CHANNEL_PACKAGE_MAP: &'static str = "channel_package_map.json";

pub async fn build_current_channel_manager_and_notifier<S: ServiceConnect>(
    service_connector: S,
) -> Result<(CurrentChannelManager, CurrentChannelNotifier<S>), anyhow::Error> {
    let (current_channel, current_realm) = if let (Some(channel), Some(realm)) =
        lookup_channel_and_realm_from_vbmeta(&service_connector).await.unwrap_or_else(|e| {
            fx_log_warn!("Failed to read current_channel from vbmeta: {:#}", anyhow!(e));
            (None, None)
        }) {
        (channel, Some(realm))
    } else {
        (
            lookup_channel_from_rewrite_engine(&service_connector).await.unwrap_or_else(|e| {
                fx_log_warn!(
                    "Failed to read current_channel from rewrite engine: {:#}",
                    anyhow!(e)
                );
                String::new()
            }),
            None,
        )
    };

    Ok((
        CurrentChannelManager::new(current_channel.clone()),
        CurrentChannelNotifier::new(service_connector, current_channel, current_realm),
    ))
}

pub struct CurrentChannelNotifier<S = ServiceConnector> {
    service_connector: S,
    channel: String,
    realm: Option<String>,
}

impl<S: ServiceConnect> CurrentChannelNotifier<S> {
    fn new(service_connector: S, channel: String, realm: Option<String>) -> Self {
        CurrentChannelNotifier { service_connector, channel, realm }
    }

    async fn notify_cobalt(
        service_connector: &S,
        current_channel: String,
        current_realm: Option<String>,
    ) {
        loop {
            let cobalt = Self::connect(service_connector).await;
            let distribution_info = SoftwareDistributionInfo {
                current_channel: Some(current_channel.clone()),
                current_realm: current_realm.clone(),
                ..SoftwareDistributionInfo::EMPTY
            };

            fx_log_info!("calling cobalt.SetSoftwareDistributionInfo(\"{:?}\")", distribution_info);

            match cobalt.set_software_distribution_info(distribution_info).await {
                Ok(CobaltStatus::Ok) => {
                    return;
                }
                Ok(CobaltStatus::EventTooBig) => {
                    fx_log_warn!(
                        "cobalt.SetSoftwareDistributionInfo returned Status.EVENT_TOO_BIG, retrying"
                    );
                }
                Ok(status) => {
                    // Not much we can do about the other status codes but log.
                    fx_log_err!(
                        "cobalt.SetSoftwareDistributionInfo returned non-OK status: {:?}",
                        status
                    );
                    return;
                }
                Err(err) => {
                    // channel broken, so log the error and reconnect.
                    fx_log_warn!(
                        "cobalt.SetSoftwareDistributionInfo returned error: {:#}, retrying",
                        anyhow!(err)
                    );
                }
            }

            Self::sleep().await;
        }
    }

    pub async fn run(self) {
        let Self { service_connector, channel, realm } = self;
        Self::notify_cobalt(&service_connector, channel, realm).await;
    }

    async fn connect(service_connector: &S) -> SystemDataUpdaterProxy {
        loop {
            match service_connector.connect_to_service::<SystemDataUpdaterMarker>() {
                Ok(cobalt) => {
                    return cobalt;
                }
                Err(err) => {
                    fx_log_err!("error connecting to cobalt: {:#}", anyhow!(err));
                    Self::sleep().await
                }
            }
        }
    }

    async fn sleep() {
        fasync::Timer::new(Duration::from_secs(5)).await;
    }
}

#[derive(Clone)]
pub struct CurrentChannelManager {
    channel: String,
}

impl CurrentChannelManager {
    pub fn new(channel: String) -> Self {
        CurrentChannelManager { channel }
    }

    pub fn read_current_channel(&self) -> Result<String, Error> {
        Ok(self.channel.clone())
    }
}

pub struct TargetChannelManager<S = ServiceConnector> {
    service_connector: S,
    path: PathBuf,
    target_channel: Mutex<Option<String>>,
    channel_package_map: HashMap<String, PkgUrl>,
}

impl<S: ServiceConnect> TargetChannelManager<S> {
    /// Create a new |TargetChannelManager|.
    ///
    /// Arguments:
    /// * `service_connector` - used to connect to fuchsia.pkg.RepositoryManager and
    ///   fuchsia.boot.ArgumentsMarker.
    /// * `dir` - directory containing mutable configuration files (current and target channel).
    ///   Usually /data/misc/ota.
    /// * `config_dir` - directory containing immutable configuration, usually /config/data.
    pub fn new(
        service_connector: S,
        dir: impl Into<PathBuf>,
        config_dir: impl Into<PathBuf>,
    ) -> Self {
        let mut path = dir.into();
        path.push(TARGET_CHANNEL);
        let target_channel = Mutex::new(read_channel(&path).ok());
        let mut config_path = config_dir.into();
        config_path.push(CHANNEL_PACKAGE_MAP);
        let channel_package_map = read_channel_mappings(&config_path).unwrap_or_else(|err| {
            fx_log_warn!("Failed to load {}: {:?}", CHANNEL_PACKAGE_MAP, err);
            HashMap::new()
        });

        Self { service_connector, path, target_channel, channel_package_map }
    }

    /// Fetch the target channel from vbmeta, if one is present.
    /// Otherwise, will attempt to guess channel from the current rewrite rules.
    pub async fn update(&self) -> Result<(), anyhow::Error> {
        let (target_channel, _) =
            lookup_channel_and_realm_from_vbmeta(&self.service_connector).await?;
        if target_channel.is_some()
            && self.target_channel.lock().as_ref() == target_channel.as_ref()
        {
            return Ok(());
        }

        if let Some(channel) = target_channel {
            self.set_target_channel(channel).await?;
            return Ok(());
        }

        // If no vbmeta channel is present, try using the rewrite rules.
        // This ensures that the target channel is a sensible value for local builds.
        let target_channel = lookup_channel_from_rewrite_engine(&self.service_connector).await?;
        if self.target_channel.lock().as_ref() == Some(&target_channel) {
            return Ok(());
        }

        self.set_target_channel(target_channel).await
    }

    pub fn get_target_channel(&self) -> Option<String> {
        self.target_channel.lock().clone()
    }

    /// Returns the update URL for the current target channel, if the channel exists.
    pub fn get_target_channel_update_url(&self) -> Option<String> {
        let target_channel = self.get_target_channel()?;
        match self.channel_package_map.get(&target_channel) {
            Some(url) => Some(url.to_string()),
            None => Some(format!("fuchsia-pkg://{}/update", target_channel)),
        }
    }

    pub async fn set_target_channel(&self, channel: String) -> Result<(), anyhow::Error> {
        // Write to target_channel.json
        write_channel(&self.path, &channel)?;

        // Save it to self.target_channel to be consistent with target_channel.json.
        *self.target_channel.lock() = Some(channel.clone());

        Ok(())
    }

    pub async fn get_channel_list(&self) -> Result<Vec<String>, anyhow::Error> {
        let repository_manager =
            self.service_connector.connect_to_service::<RepositoryManagerMarker>()?;
        let (repo_iterator, server_end) = fidl::endpoints::create_proxy()?;
        repository_manager.list(server_end)?;
        let mut repo_configs = vec![];
        loop {
            let repos = repo_iterator.next().await?;
            if repos.is_empty() {
                break;
            }
            repo_configs.extend(repos);
        }
        let mut channels: HashSet<String> = repo_configs
            .into_iter()
            .filter_map(|config| config.try_into().ok())
            .map(|config: RepositoryConfig| config.repo_url().host().to_string())
            .collect();

        // We want to have the final list of channels include any user-added channels (e.g.
        // "devhost"). To achieve this, only remove channels which have a corresponding entry in
        // the channel->package map.
        for (channel, package) in self.channel_package_map.iter() {
            channels.remove(package.host());
            channels.insert(channel.clone());
        }

        let mut result = channels.into_iter().collect::<Vec<String>>();
        result.sort();
        Ok(result)
    }
}

/// Uses fuchsia.rewrite.Engine/TestApply on 'fuchsia-pkg://fuchsia.com/update/0' to determine
/// the current channel.
async fn lookup_channel_from_rewrite_engine(
    service_connector: &impl ServiceConnect,
) -> Result<String, anyhow::Error> {
    let rewrite_engine = service_connector.connect_to_service::<EngineMarker>()?;
    let rewritten: PkgUrl = rewrite_engine
        .test_apply("fuchsia-pkg://fuchsia.com/update/0")
        .await?
        .map_err(|s| zx::Status::from_raw(s))?
        .parse()?;
    let channel = rewritten.repo().channel().unwrap_or_else(|| rewritten.host());

    Ok(channel.to_owned())
}

/// Uses Zircon kernel arguments (typically provided by vbmeta) to determine the current channel.
async fn lookup_channel_and_realm_from_vbmeta(
    service_connector: &impl ServiceConnect,
) -> Result<(Option<String>, Option<String>), anyhow::Error> {
    let proxy = service_connector.connect_to_service::<ArgumentsMarker>()?;
    let options = ["ota_channel", "omaha_app_id"];
    // Rust doesn't like it if we try to .await? on the same line.
    let future = proxy.get_strings(&mut options.iter().copied());
    let results = future.await?;

    if results.len() != 2 {
        return Err(anyhow!("Wrong number of results for get_strings()"));
    }

    Ok((results[0].clone(), results[1].clone()))
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum Channel {
    #[serde(rename = "1")]
    Version1 { legacy_amber_source_name: String },
}

fn read_channel(path: impl AsRef<Path>) -> Result<String, Error> {
    let f = fs::File::open(path.as_ref())?;
    match serde_json::from_reader(io::BufReader::new(f))? {
        Channel::Version1 { legacy_amber_source_name } => Ok(legacy_amber_source_name),
    }
}

fn write_channel(path: impl AsRef<Path>, channel: impl Into<String>) -> Result<(), anyhow::Error> {
    let path = path.as_ref();
    let channel = Channel::Version1 { legacy_amber_source_name: channel.into() };

    let mut temp_path = path.to_owned().into_os_string();
    temp_path.push(".new");
    let temp_path = PathBuf::from(temp_path);
    {
        if let Some(dir) = temp_path.parent() {
            fs::create_dir_all(dir).with_context(|| format!("create_dir_all {:?}", dir))?;
        }
        let mut f = io::BufWriter::new(
            fs::File::create(&temp_path)
                .with_context(|| format!("create temp file {:?}", temp_path))?,
        );
        serde_json::to_writer(&mut f, &channel).context("serialize config")?;
        f.flush().context("flush")?;
    };
    fs::rename(&temp_path, path).with_context(|| format!("rename {:?} to {:?}", temp_path, path))
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
pub enum ChannelPackageMap {
    #[serde(rename = "1")]
    Version1(Vec<ChannelPackagePair>),
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ChannelPackagePair {
    channel: String,
    package: PkgUrl,
}

fn read_channel_mappings(p: impl AsRef<Path>) -> Result<HashMap<String, PkgUrl>, Error> {
    let f = fs::File::open(p.as_ref())?;
    let mut result = HashMap::new();
    match serde_json::from_reader(io::BufReader::new(f))? {
        ChannelPackageMap::Version1(items) => {
            for item in items.into_iter() {
                if let Some(old_pkg) = result.insert(item.channel.clone(), item.package.clone()) {
                    fx_log_err!(
                        "Duplicate update package definition for channel {}: {} and {}.",
                        item.channel,
                        item.package,
                        old_pkg
                    );
                }
            }
        }
    };

    Ok(result)
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("io error")]
    Io(#[from] io::Error),

    #[error("json error")]
    Json(#[from] serde_json::Error),
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::{DiscoverableProtocolMarker, RequestStream};
    use fidl_fuchsia_boot::{ArgumentsRequest, ArgumentsRequestStream};
    use fidl_fuchsia_cobalt::{SystemDataUpdaterRequest, SystemDataUpdaterRequestStream};
    use fidl_fuchsia_pkg::{
        RepositoryIteratorRequest, RepositoryManagerRequest, RepositoryManagerRequestStream,
    };
    use fidl_fuchsia_pkg_ext::RepositoryConfigBuilder;
    use fidl_fuchsia_pkg_rewrite::{EngineRequest, EngineRequestStream};
    use fuchsia_async::DurationExt;
    use fuchsia_component::server::ServiceFs;
    use fuchsia_url::pkg_url::RepoUrl;
    use fuchsia_zircon::DurationNum;
    use futures::prelude::*;
    use futures::task::Poll;
    use futures::{future::FutureExt, stream::StreamExt};
    use matches::assert_matches;
    use parking_lot::Mutex;
    use serde_json::{json, Value};
    use std::sync::Arc;
    use tempfile;

    #[test]
    fn test_write_channel() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("channel.json");

        assert_matches!(write_channel(&path, "test"), Ok(()));

        let f = fs::File::open(path).expect("file to exist");
        let value: Value = serde_json::from_reader(f).expect("valid json");
        assert_eq!(
            value,
            json!({
                "version": "1",
                "content": {
                    "legacy_amber_source_name": "test",
                }
            })
        );
    }

    #[test]
    fn test_write_channel_create_subdir() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("subdir").join("channel.json");

        assert_matches!(write_channel(&path, "test"), Ok(()));

        let f = fs::File::open(path).expect("file to exist");
        let value: Value = serde_json::from_reader(f).expect("valid json");
        assert_eq!(
            value,
            json!({
                "version": "1",
                "content": {
                    "legacy_amber_source_name": "test",
                }
            })
        );
    }

    fn serve_ota_channel_arguments(
        mut stream: ArgumentsRequestStream,
        channel: &'static str,
        realm: Option<&'static str>,
    ) -> fasync::Task<()> {
        fasync::Task::local(async move {
            while let Some(req) = stream.try_next().await.unwrap_or(None) {
                match req {
                    ArgumentsRequest::GetStrings { keys, responder } => {
                        assert_eq!(keys, vec!["ota_channel", "omaha_app_id"]);
                        let response = [Some(channel), realm.clone()];
                        responder.send(&mut response.iter().map(|v| *v)).expect("send ok");
                    }
                    _ => unreachable!(),
                }
            }
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_current_channel_notifier() {
        let (connector, svc_dir) =
            NamespacedServiceConnector::bind("/test/current_channel_manager/svc")
                .expect("ns to bind");

        let mut fs = ServiceFs::new_local();
        let channel_and_realm = Arc::new(Mutex::new((None, None)));
        let chan = channel_and_realm.clone();

        fs.add_fidl_service(move |mut stream: SystemDataUpdaterRequestStream| {
            let chan = chan.clone();

            fasync::Task::local(async move {
                while let Some(req) = stream.try_next().await.unwrap_or(None) {
                    match req {
                        SystemDataUpdaterRequest::SetSoftwareDistributionInfo {
                            info,
                            responder,
                        } => {
                            *chan.lock() = (info.current_channel, info.current_realm);
                            responder.send(CobaltStatus::Ok).unwrap();
                        }
                        _ => unreachable!(),
                    }
                }
            })
            .detach()
        })
        .add_fidl_service(move |stream: ArgumentsRequestStream| {
            serve_ota_channel_arguments(stream, "stable", Some("sample-realm")).detach()
        })
        .serve_connection(svc_dir)
        .expect("serve_connection");

        fasync::Task::local(fs.collect()).detach();
        let (_, c) = build_current_channel_manager_and_notifier(connector).await.unwrap();

        c.run().await;

        let lock = channel_and_realm.lock();
        assert_eq!(lock.0.as_deref(), Some("stable"));
        assert_eq!(lock.1.as_deref(), Some("sample-realm"));
    }

    #[test]
    fn test_current_channel_notifier_retries() {
        #[derive(Debug, Clone)]
        enum FlakeMode {
            ErrorOnConnect,
            DropConnection,
            StatusOnCall(CobaltStatus),
        }

        #[derive(Debug, Clone)]
        struct State {
            mode: Option<FlakeMode>,
            channel: Option<String>,
            realm: Option<String>,
            connect_count: u64,
            call_count: u64,
        }

        #[derive(Clone, Debug)]
        struct FlakeyServiceConnector {
            state: Arc<Mutex<State>>,
        }

        impl FlakeyServiceConnector {
            fn new() -> Self {
                Self {
                    state: Arc::new(Mutex::new(State {
                        mode: Some(FlakeMode::ErrorOnConnect),
                        channel: None,
                        realm: None,
                        connect_count: 0,
                        call_count: 0,
                    })),
                }
            }
            fn set_flake_mode(&self, mode: impl Into<Option<FlakeMode>>) {
                self.state.lock().mode = mode.into();
            }
            fn channel(&self) -> Option<String> {
                self.state.lock().channel.clone()
            }
            fn realm(&self) -> Option<String> {
                self.state.lock().realm.clone()
            }
            fn connect_count(&self) -> u64 {
                self.state.lock().connect_count
            }
            fn call_count(&self) -> u64 {
                self.state.lock().call_count
            }
        }

        impl ServiceConnect for FlakeyServiceConnector {
            fn connect_to_service<P: DiscoverableProtocolMarker>(
                &self,
            ) -> Result<P::Proxy, anyhow::Error> {
                let mode = if P::PROTOCOL_NAME == SystemDataUpdaterMarker::PROTOCOL_NAME {
                    // Only flake connections to cobalt.
                    self.state.lock().connect_count += 1;
                    self.state.lock().mode.clone()
                } else {
                    None
                };
                match mode {
                    Some(FlakeMode::ErrorOnConnect) => {
                        return Err(anyhow::format_err!("test error on connect"))
                    }
                    Some(FlakeMode::DropConnection) => {
                        let (proxy, _stream) = fidl::endpoints::create_proxy::<P>().unwrap();
                        Ok(proxy)
                    }
                    Some(FlakeMode::StatusOnCall(status)) => {
                        let (proxy, stream) =
                            fidl::endpoints::create_proxy_and_stream::<P>().unwrap();
                        let mut stream: SystemDataUpdaterRequestStream = stream.cast_stream();

                        let state = self.state.clone();
                        fasync::Task::local(async move {
                            while let Some(req) = stream.try_next().await.unwrap() {
                                match req {
                                    SystemDataUpdaterRequest::SetSoftwareDistributionInfo {
                                        info: _info,
                                        responder,
                                    } => {
                                        state.lock().call_count += 1;
                                        responder.send(status).unwrap();
                                    }
                                    _ => unreachable!(),
                                }
                            }
                        })
                        .detach();
                        Ok(proxy)
                    }
                    None => {
                        let (proxy, stream) =
                            fidl::endpoints::create_proxy_and_stream::<P>().unwrap();

                        match P::PROTOCOL_NAME {
                            SystemDataUpdaterMarker::PROTOCOL_NAME => {
                                let mut stream: SystemDataUpdaterRequestStream =
                                    stream.cast_stream();

                                let state = self.state.clone();
                                fasync::Task::local(async move {
                                    while let Some(req) = stream.try_next().await.unwrap() {
                                        match req {
                                        SystemDataUpdaterRequest::SetSoftwareDistributionInfo {
                                            info,
                                            responder,
                                        } => {
                                            state.lock().call_count += 1;
                                            state.lock().channel = info.current_channel;
                                            state.lock().realm = info.current_realm;
                                            responder.send(CobaltStatus::Ok).unwrap();
                                        }
                                        _ => unreachable!(),
                                    }
                                    }
                                })
                                .detach();
                            }
                            ArgumentsMarker::PROTOCOL_NAME => {
                                serve_ota_channel_arguments(
                                    stream.cast_stream(),
                                    "stable",
                                    Some("sample-realm"),
                                )
                                .detach();
                            }
                            _ => unimplemented!(),
                        };
                        Ok(proxy)
                    }
                }
            }
        }

        let connector = FlakeyServiceConnector::new();
        let future = build_current_channel_manager_and_notifier(connector.clone());
        let mut real_executor = fasync::TestExecutor::new().expect("new executor ok");
        let (_, c) =
            real_executor.run_singlethreaded(future).expect("failed to construct channel_manager");
        std::mem::drop(real_executor);
        let mut task = c.run().boxed();
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        assert_eq!(executor.run_until_stalled(&mut task), Poll::Pending);

        // Retries if connecting fails
        assert_eq!(executor.wake_expired_timers(), false);
        executor.set_fake_time(5.seconds().after_now());
        assert_eq!(executor.wake_expired_timers(), true);
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Pending);
        assert_eq!(connector.connect_count(), 2);

        // Retries if a fidl error occurs during the request
        connector.set_flake_mode(FlakeMode::DropConnection);
        executor.set_fake_time(5.seconds().after_now());
        assert_eq!(executor.wake_expired_timers(), true);
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Pending);
        assert_eq!(connector.connect_count(), 3);

        // Retries on expected Cobalt error status codes
        connector.set_flake_mode(FlakeMode::StatusOnCall(CobaltStatus::EventTooBig));
        executor.set_fake_time(5.seconds().after_now());
        assert_eq!(connector.call_count(), 0);
        assert_eq!(executor.wake_expired_timers(), true);
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Pending);
        assert_eq!(connector.connect_count(), 4);
        assert_eq!(connector.call_count(), 1);

        // Stops trying when it eventually succeeds
        connector.set_flake_mode(None);
        executor.set_fake_time(5.seconds().after_now());
        assert_eq!(executor.wake_expired_timers(), true);
        assert_eq!(connector.channel(), None);
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Ready(()));
        assert_eq!(connector.connect_count(), 5);
        assert_eq!(connector.call_count(), 2);
        assert_eq!(connector.channel(), Some("stable".to_owned()));
        assert_eq!(connector.realm(), Some("sample-realm".to_owned()));

        std::mem::drop(executor);
        let mut real_executor = fasync::TestExecutor::new().expect("new executor ok");
        // Bails out if Cobalt responds with an unexpected status code
        let connector = FlakeyServiceConnector::new();
        let future = build_current_channel_manager_and_notifier(connector.clone());
        let (_, c) =
            real_executor.run_singlethreaded(future).expect("failed to construct channel_manager");
        std::mem::drop(real_executor);
        let mut task = c.run().boxed();
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        connector.set_flake_mode(FlakeMode::StatusOnCall(CobaltStatus::InvalidArguments));
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Ready(()));
        assert_eq!(connector.connect_count(), 1);
        assert_eq!(connector.call_count(), 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_writes_channel() {
        let dir = tempfile::tempdir().unwrap();
        let target_channel_path = dir.path().join(TARGET_CHANNEL);

        let connector = ArgumentsServiceConnector::new("fuchsia-pkg://devhost/update/0", None);
        let channel_manager = TargetChannelManager::new(connector.clone(), dir.path(), dir.path());

        // First write of the file with the correct data.
        assert_matches!(read_channel(&target_channel_path), Err(_));
        channel_manager.update().await.expect("channel update to succeed");
        assert_eq!(channel_manager.get_target_channel(), Some("devhost".to_string()));
        assert_eq!(read_channel(&target_channel_path).unwrap(), "devhost");

        // If the file changes while the service is running, an update doesn't know to replace it.
        write_channel(&target_channel_path, "unique").unwrap();
        channel_manager.update().await.expect("channel update to succeed");
        assert_eq!(channel_manager.get_target_channel(), Some("devhost".to_string()));
        assert_eq!(read_channel(&target_channel_path).unwrap(), "unique");

        // If the update package changes, or our vbmeta changes, the file will be updated.
        connector.set(Some("world".to_owned()));
        channel_manager.update().await.expect("channel update to succeed");
        assert_eq!(channel_manager.get_target_channel(), Some("world".to_string()));
        assert_eq!(read_channel(&target_channel_path).unwrap(), "world");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_recovers_from_corrupt_data() {
        let dir = tempfile::tempdir().unwrap();
        let target_channel_path = dir.path().join(TARGET_CHANNEL);

        fs::write(&target_channel_path, r#"invalid json"#).unwrap();

        let connector =
            ArgumentsServiceConnector::new("fuchsia-pkg://devhost/update/0", Some("b".to_string()));
        let channel_manager = TargetChannelManager::new(connector, dir.path(), dir.path());

        assert!(read_channel(&target_channel_path).is_err());
        channel_manager.update().await.expect("channel update to succeed");
        assert_eq!(channel_manager.get_target_channel(), Some("b".to_string()));
        assert_eq!(read_channel(&target_channel_path).unwrap(), "b");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_set_target_channel() {
        let dir = tempfile::tempdir().unwrap();
        let target_channel_path = dir.path().join(TARGET_CHANNEL);

        let connector = ArgumentsServiceConnector::new(
            "fuchsia-pkg://devhost/update/0",
            Some("not-target-channel".to_string()),
        );
        let channel_manager = TargetChannelManager::new(connector, dir.path(), dir.path());
        channel_manager.set_target_channel("target-channel".to_string()).await.unwrap();
        assert_eq!(channel_manager.get_target_channel(), Some("target-channel".to_string()));
        assert_eq!(read_channel(&target_channel_path).unwrap(), "target-channel");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_update_uses_vbmeta() {
        let dir = tempfile::tempdir().unwrap();

        let connector = ArgumentsServiceConnector::new(
            "fuchsia-pkg://devhost/update/0",
            Some("not-devhost".to_string()),
        );
        let channel_manager = TargetChannelManager::new(connector, dir.path(), dir.path());
        channel_manager.update().await.unwrap();
        assert_eq!(channel_manager.get_target_channel(), Some("not-devhost".to_string()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_update_falls_back_to_rewrite_engine() {
        let dir = tempfile::tempdir().unwrap();

        let connector = ArgumentsServiceConnector::new(
            "fuchsia-pkg://my-cool-package-server.example.com/update/0",
            None,
        );
        let channel_manager = TargetChannelManager::new(connector, dir.path(), dir.path());
        channel_manager.update().await.unwrap();
        assert_eq!(
            channel_manager.get_target_channel(),
            Some("my-cool-package-server.example.com".to_string())
        );
    }

    #[derive(Clone)]
    struct ArgumentsServiceConnector {
        ota_channel: Arc<Mutex<Option<String>>>,
        update_url: Arc<Mutex<String>>,
    }

    impl ArgumentsServiceConnector {
        fn new<'a>(update_url: impl Into<String>, ota_channel: Option<String>) -> Self {
            Self {
                ota_channel: Arc::new(Mutex::new(ota_channel)),
                update_url: Arc::new(Mutex::new(update_url.into())),
            }
        }
        fn set(&self, target: Option<String>) {
            *self.ota_channel.lock() = target;
        }
        fn handle_arguments_stream(&self, mut stream: ArgumentsRequestStream) {
            let channel = self.ota_channel.lock().clone();
            fasync::Task::local(async move {
                while let Some(req) = stream.try_next().await.unwrap() {
                    match req {
                        ArgumentsRequest::GetStrings { keys, responder } => {
                            assert_eq!(keys, vec!["ota_channel", "omaha_app_id"]);
                            let response = vec![channel.as_deref(), None];
                            responder.send(&mut response.into_iter()).unwrap();
                        }
                        _ => unreachable!(),
                    }
                }
            })
            .detach();
        }

        fn handle_engine_stream(&self, mut stream: EngineRequestStream) {
            let target = self.update_url.lock().clone();
            fasync::Task::local(async move {
                while let Some(req) = stream.try_next().await.unwrap() {
                    match req {
                        EngineRequest::TestApply { url, responder } => {
                            assert_eq!(url, "fuchsia-pkg://fuchsia.com/update/0");

                            responder.send(&mut Ok(target.clone())).unwrap();
                        }
                        _ => unreachable!(),
                    }
                }
            })
            .detach();
        }
    }

    impl ServiceConnect for ArgumentsServiceConnector {
        fn connect_to_service<P: DiscoverableProtocolMarker>(
            &self,
        ) -> Result<P::Proxy, anyhow::Error> {
            let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<P>().unwrap();
            match P::PROTOCOL_NAME {
                ArgumentsMarker::PROTOCOL_NAME => {
                    self.handle_arguments_stream(stream.cast_stream())
                }
                EngineMarker::PROTOCOL_NAME => self.handle_engine_stream(stream.cast_stream()),
                _ => panic!("Unsupported service {}", P::DEBUG_NAME),
            }
            Ok(proxy)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_get_update_package_url() {
        let dir = tempfile::tempdir().unwrap();
        let connector = RepoMgrServiceConnector {
            channels: vec!["asdfghjkl.example.com", "qwertyuiop.example.com", "devhost"],
        };

        let package_map_path = dir.path().join(CHANNEL_PACKAGE_MAP);

        fs::write(&package_map_path,
            r#"{"version":"1","content":[{"channel":"first","package":"fuchsia-pkg://asdfghjkl.example.com/update"}]}"#,
        ).unwrap();

        let channel_manager = TargetChannelManager::new(connector, dir.path(), dir.path());
        assert_eq!(channel_manager.get_target_channel_update_url(), None);
        channel_manager.set_target_channel("first".to_owned()).await.unwrap();
        assert_eq!(
            channel_manager.get_target_channel_update_url(),
            Some("fuchsia-pkg://asdfghjkl.example.com/update".to_owned())
        );

        channel_manager.set_target_channel("does_not_exist".to_owned()).await.unwrap();
        assert_eq!(
            channel_manager.get_target_channel_update_url(),
            Some("fuchsia-pkg://does_not_exist/update".to_owned())
        );

        channel_manager.set_target_channel("qwertyuiop.example.com".to_owned()).await.unwrap();
        assert_eq!(
            channel_manager.get_target_channel_update_url(),
            Some("fuchsia-pkg://qwertyuiop.example.com/update".to_owned())
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_get_channel_list_with_map() {
        let dir = tempfile::tempdir().unwrap();
        let connector = RepoMgrServiceConnector {
            channels: vec!["asdfghjkl.example.com", "qwertyuiop.example.com", "devhost"],
        };

        let package_map_path = dir.path().join(CHANNEL_PACKAGE_MAP);

        fs::write(&package_map_path,
            r#"{"version":"1","content":[{"channel":"first","package":"fuchsia-pkg://asdfghjkl.example.com/update"}]}"#,
        ).unwrap();

        let channel_manager = TargetChannelManager::new(connector, dir.path(), dir.path());
        assert_eq!(
            channel_manager.get_channel_list().await.unwrap(),
            vec!["devhost", "first", "qwertyuiop.example.com"]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_get_channel_list() {
        let dir = tempfile::tempdir().unwrap();
        let connector =
            RepoMgrServiceConnector { channels: vec!["some-channel", "target-channel"] };
        let channel_manager = TargetChannelManager::new(connector, dir.path(), dir.path());
        assert_eq!(
            channel_manager.get_channel_list().await.unwrap(),
            vec!["some-channel", "target-channel"]
        );
    }

    #[derive(Clone)]
    struct RepoMgrServiceConnector {
        channels: Vec<&'static str>,
    }

    impl ServiceConnect for RepoMgrServiceConnector {
        fn connect_to_service<P: DiscoverableProtocolMarker>(
            &self,
        ) -> Result<P::Proxy, anyhow::Error> {
            let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<P>().unwrap();
            assert_eq!(P::PROTOCOL_NAME, RepositoryManagerMarker::PROTOCOL_NAME);
            let mut stream: RepositoryManagerRequestStream = stream.cast_stream();
            let channels = self.channels.clone();

            fasync::Task::local(async move {
                while let Some(req) = stream.try_next().await.unwrap() {
                    match req {
                        RepositoryManagerRequest::List { iterator, control_handle: _ } => {
                            let mut stream = iterator.into_stream().unwrap();
                            let repos: Vec<_> = channels
                                .iter()
                                .map(|channel| {
                                    RepositoryConfigBuilder::new(
                                        RepoUrl::new(channel.to_string()).unwrap(),
                                    )
                                    .build()
                                    .into()
                                })
                                .collect();

                            fasync::Task::local(async move {
                                let mut iter = repos.into_iter();

                                while let Some(RepositoryIteratorRequest::Next { responder }) =
                                    stream.try_next().await.unwrap()
                                {
                                    responder.send(&mut iter.by_ref().take(1)).unwrap();
                                }
                            })
                            .detach();
                        }
                        _ => unreachable!(),
                    }
                }
            })
            .detach();
            Ok(proxy)
        }
    }
}
