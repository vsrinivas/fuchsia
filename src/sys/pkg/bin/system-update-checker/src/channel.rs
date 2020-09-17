// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::connect::*;
use anyhow::anyhow;
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
use futures::{
    channel::mpsc,
    future::{self, Either, FutureExt},
    sink::SinkExt,
    stream::StreamExt,
};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use serde_json;
use std::convert::TryInto;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::time::Duration;
use sysconfig_client::channel::OtaUpdateChannelConfig;
use thiserror::Error;

static CURRENT_CHANNEL: &'static str = "current_channel.json";
static TARGET_CHANNEL: &'static str = "target_channel.json";

pub fn build_current_channel_manager_and_notifier<S: ServiceConnect>(
    service_connector: S,
    dir: impl Into<PathBuf>,
) -> Result<(CurrentChannelManager, CurrentChannelNotifier<S>), anyhow::Error> {
    let path = dir.into();
    let current_channel = read_current_channel(path.as_ref()).unwrap_or_else(|err| {
            fx_log_err!(
                "Error reading current_channel, defaulting to the empty string. This is expected before the first OTA. {:#}",
                anyhow!(err)
            );
            String::new()
        });

    let (channel_sender, channel_receiver) = mpsc::channel(100);

    Ok((
        CurrentChannelManager::new(path, channel_sender),
        CurrentChannelNotifier::new(service_connector, current_channel, channel_receiver),
    ))
}

pub struct CurrentChannelNotifier<S = ServiceConnector> {
    service_connector: S,
    initial_channel: String,
    channel_receiver: mpsc::Receiver<String>,
}

impl<S: ServiceConnect> CurrentChannelNotifier<S> {
    fn new(
        service_connector: S,
        initial_channel: String,
        channel_receiver: mpsc::Receiver<String>,
    ) -> Self {
        CurrentChannelNotifier { service_connector, initial_channel, channel_receiver }
    }

    async fn notify_cobalt(service_connector: &S, current_channel: String) {
        loop {
            let cobalt = Self::connect(service_connector).await;
            let distribution_info = SoftwareDistributionInfo {
                current_channel: Some(current_channel.clone()),
                // The realm field allows the omaha_client to pass the Omaha app_id to Cobalt.
                // Here we have no need to populate the realm field.
                current_realm: None,
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
        let Self { service_connector, initial_channel, mut channel_receiver } = self;
        let mut notify_cobalt_task =
            Self::notify_cobalt(&service_connector, initial_channel).boxed();

        loop {
            match future::select(channel_receiver.next(), notify_cobalt_task).await {
                Either::Left((Some(current_channel), _)) => {
                    fx_log_warn!(
                        "notify_cobalt() overrun. Starting again with new channel: `{}`",
                        current_channel
                    );
                    notify_cobalt_task =
                        Self::notify_cobalt(&service_connector, current_channel).boxed();
                }
                Either::Left((None, notify_cobalt_future)) => {
                    fx_log_warn!(
                        "all channel_senders have been closed. No new messages will arrive."
                    );
                    notify_cobalt_future.await;
                    return;
                }
                Either::Right((_, next_channel_fut)) => {
                    if let Some(current_channel) = next_channel_fut.await {
                        notify_cobalt_task =
                            Self::notify_cobalt(&service_connector, current_channel).boxed();
                    } else {
                        fx_log_warn!(
                            "all channel_senders have been closed. No new messages will arrive."
                        );
                        return;
                    }
                }
            }
        }
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
        let delay = fasync::Time::after(Duration::from_secs(5).into());
        fasync::Timer::new(delay).await;
    }
}

#[derive(Clone)]
pub struct CurrentChannelManager {
    path: PathBuf,
    channel_sender: mpsc::Sender<String>,
}

impl CurrentChannelManager {
    pub fn new(path: PathBuf, channel_sender: mpsc::Sender<String>) -> Self {
        CurrentChannelManager { path, channel_sender }
    }

    pub async fn update(&self) -> Result<(), anyhow::Error> {
        let target_channel = read_channel(&self.path.join(TARGET_CHANNEL))?;
        if target_channel != self.read_current_channel().ok().unwrap_or_else(String::new) {
            write_channel(&self.path.join(CURRENT_CHANNEL), &target_channel)?;
            self.channel_sender.clone().send(target_channel).await?;
        }
        Ok(())
    }

    pub fn read_current_channel(&self) -> Result<String, Error> {
        read_current_channel(&self.path)
    }
}

pub struct TargetChannelManager<S = ServiceConnector> {
    service_connector: S,
    path: PathBuf,
    target_channel: Mutex<Option<String>>,
}

impl<S: ServiceConnect> TargetChannelManager<S> {
    pub fn new(service_connector: S, dir: impl Into<PathBuf>) -> Self {
        let mut path = dir.into();
        path.push(TARGET_CHANNEL);
        let target_channel = Mutex::new(read_channel(&path).ok());

        Self { service_connector, path, target_channel }
    }

    pub async fn update(&self) -> Result<(), anyhow::Error> {
        let target_channel = self.lookup_target_channel().await?;
        if self.target_channel.lock().as_ref() == Some(&target_channel) {
            return Ok(());
        }

        self.set_target_channel(target_channel).await
    }

    async fn lookup_target_channel(&self) -> Result<String, anyhow::Error> {
        let rewrite_engine = self.service_connector.connect_to_service::<EngineMarker>()?;
        let rewritten: PkgUrl = rewrite_engine
            .test_apply("fuchsia-pkg://fuchsia.com/update/0")
            .await?
            .map_err(|s| zx::Status::from_raw(s))?
            .parse()?;
        let channel = rewritten.repo().channel().unwrap_or(rewritten.host());

        Ok(channel.to_owned())
    }

    pub fn get_target_channel(&self) -> Option<String> {
        self.target_channel.lock().clone()
    }

    pub async fn set_target_channel(&self, channel: String) -> Result<(), anyhow::Error> {
        // Write to target_channel.json
        write_channel(&self.path, &channel)?;

        // Save it to self.target_channel before writing to sysconfig (which might fail) to be
        // consistent with target_channel.json.
        *self.target_channel.lock() = Some(channel.clone());

        // Write to sysconfig
        let config = OtaUpdateChannelConfig::new(&channel, &channel)?;
        write_channel_config(&config).await?;
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
        Ok(repo_configs
            .into_iter()
            .filter_map(|config| config.try_into().ok())
            .filter_map(|config: RepositoryConfig| {
                config.repo_url().channel().map(|s| s.to_string())
            })
            .collect())
    }
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum Channel {
    #[serde(rename = "1")]
    Version1 { legacy_amber_source_name: String },
}

fn read_current_channel(p: &Path) -> Result<String, Error> {
    read_channel(p.join(CURRENT_CHANNEL))
}

fn read_channel(path: impl AsRef<Path>) -> Result<String, Error> {
    let f = fs::File::open(path.as_ref())?;
    match serde_json::from_reader(io::BufReader::new(f))? {
        Channel::Version1 { legacy_amber_source_name } => Ok(legacy_amber_source_name),
    }
}

fn write_channel(path: impl AsRef<Path>, channel: impl Into<String>) -> Result<(), io::Error> {
    let path = path.as_ref();
    let channel = Channel::Version1 { legacy_amber_source_name: channel.into() };

    let mut temp_path = path.to_owned().into_os_string();
    temp_path.push(".new");
    let temp_path = PathBuf::from(temp_path);
    {
        if let Some(dir) = temp_path.parent() {
            fs::create_dir_all(dir)?;
        }
        let f = fs::File::create(&temp_path)?;
        serde_json::to_writer(io::BufWriter::new(f), &channel)?;
    };
    fs::rename(temp_path, path)
}

#[cfg(not(test))]
use sysconfig_client::channel::write_channel_config;

#[cfg(test)]
use mock_sysconfig::write_channel_config;
#[cfg(test)]
mod mock_sysconfig {
    use super::*;

    thread_local!(static LAST_CONFIG: Mutex<Option<OtaUpdateChannelConfig>> = Mutex::new(None));

    pub async fn write_channel_config(config: &OtaUpdateChannelConfig) -> Result<(), Error> {
        LAST_CONFIG.with(|last_config| *last_config.lock() = Some(config.clone()));
        Ok(())
    }

    pub fn take_last_config() -> Option<OtaUpdateChannelConfig> {
        LAST_CONFIG.with(|last_config| last_config.lock().take())
    }
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
    use fidl::endpoints::{DiscoverableService, RequestStream};
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
    use matches::assert_matches;
    use parking_lot::Mutex;
    use serde_json::{json, Value};
    use std::sync::Arc;
    use tempfile;

    #[test]
    fn test_read_current_channel() {
        let dir = tempfile::tempdir().unwrap();

        fs::write(
            dir.path().join(CURRENT_CHANNEL),
            r#"{"version":"1","content":{"legacy_amber_source_name":"stable"}}"#,
        )
        .unwrap();

        assert_matches!(read_current_channel(dir.path()), Ok(ref channel) if channel == "stable");
    }

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

    #[test]
    fn test_read_current_channel_rejects_invalid_json() {
        let dir = tempfile::tempdir().unwrap();

        fs::write(dir.path().join(CURRENT_CHANNEL), "no channel here").unwrap();

        assert_matches!(read_current_channel(dir.path()), Err(Error::Json(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_current_channel_notifier() {
        let dir = tempfile::tempdir().unwrap();
        let current_channel_path = dir.path().join(CURRENT_CHANNEL);

        fs::write(
            &current_channel_path,
            r#"{"version":"1","content":{"legacy_amber_source_name":"stable"}}"#,
        )
        .unwrap();

        let (connector, svc_dir) =
            NamespacedServiceConnector::bind("/test/current_channel_manager/svc")
                .expect("ns to bind");
        let (_, c) = build_current_channel_manager_and_notifier(connector, dir.path()).unwrap();

        let mut fs = ServiceFs::new_local();
        let channel = Arc::new(Mutex::new(None));
        let chan = channel.clone();

        fs.add_fidl_service(move |mut stream: SystemDataUpdaterRequestStream| {
            let chan = chan.clone();

            fasync::Task::local(async move {
                while let Some(req) = stream.try_next().await.unwrap_or(None) {
                    match req {
                        SystemDataUpdaterRequest::SetSoftwareDistributionInfo {
                            info,
                            responder,
                        } => {
                            *chan.lock() = info.current_channel;
                            responder.send(CobaltStatus::Ok).unwrap();
                        }
                        _ => unreachable!(),
                    }
                }
            })
            .detach()
        })
        .serve_connection(svc_dir)
        .expect("serve_connection");

        fasync::Task::local(fs.collect()).detach();

        c.run().await;

        assert_eq!(channel.lock().as_ref().map(|s| s.as_str()), Some("stable"));
    }

    #[test]
    fn test_current_channel_notifier_retries() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();

        let dir = tempfile::tempdir().unwrap();
        let current_channel_path = dir.path().join(CURRENT_CHANNEL);

        write_channel(&current_channel_path, "stable").unwrap();

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
        };

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
            fn connect_to_service<S: DiscoverableService>(
                &self,
            ) -> Result<S::Proxy, anyhow::Error> {
                assert_eq!(S::SERVICE_NAME, SystemDataUpdaterMarker::SERVICE_NAME);
                self.state.lock().connect_count += 1;
                match self.state.lock().mode {
                    Some(FlakeMode::ErrorOnConnect) => {
                        return Err(anyhow::format_err!("test error on connect"))
                    }
                    Some(FlakeMode::DropConnection) => {
                        let (proxy, _stream) = fidl::endpoints::create_proxy::<S>().unwrap();
                        Ok(proxy)
                    }
                    Some(FlakeMode::StatusOnCall(status)) => {
                        let (proxy, stream) =
                            fidl::endpoints::create_proxy_and_stream::<S>().unwrap();
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
                            fidl::endpoints::create_proxy_and_stream::<S>().unwrap();
                        let mut stream: SystemDataUpdaterRequestStream = stream.cast_stream();

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
                        Ok(proxy)
                    }
                }
            }
        }

        let connector = FlakeyServiceConnector::new();
        let (_, c) = build_current_channel_manager_and_notifier(connector.clone(), dir.path())
            .expect("failed to construct channel_manager");
        let mut task = c.run().boxed();
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
        assert_eq!(connector.realm(), None);

        // Bails out if Cobalt responds with an unexpected status code
        let connector = FlakeyServiceConnector::new();
        let (_, c) = build_current_channel_manager_and_notifier(connector.clone(), dir.path())
            .expect("failed to construct channel_manager");
        let mut task = c.run().boxed();
        connector.set_flake_mode(FlakeMode::StatusOnCall(CobaltStatus::InvalidArguments));
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Ready(()));
        assert_eq!(connector.connect_count(), 1);
        assert_eq!(connector.call_count(), 1);
    }

    #[test]
    fn test_current_channel_manager_writes_channel() {
        let mut exec = fasync::Executor::new().expect("Unable to create executor");

        let dir = tempfile::tempdir().unwrap();
        let target_channel_path = dir.path().join(TARGET_CHANNEL);
        let current_channel_path = dir.path().join(CURRENT_CHANNEL);

        let target_connector = RewriteServiceConnector::new("fuchsia-pkg://devhost/update/0");
        let target_channel_manager =
            TargetChannelManager::new(target_connector.clone(), dir.path());

        let (current_connector, svc_dir) =
            NamespacedServiceConnector::bind("/test/current_channel_manager2/svc")
                .expect("ns to bind");
        let (current_channel_manager, current_channel_notifier) =
            build_current_channel_manager_and_notifier(current_connector, dir.path()).unwrap();

        let mut fs = ServiceFs::new_local();
        let channel = Arc::new(Mutex::new(None));
        let chan = channel.clone();

        fs.add_fidl_service(move |mut stream: SystemDataUpdaterRequestStream| {
            let chan = chan.clone();

            fasync::Task::local(async move {
                while let Some(req) = stream.try_next().await.unwrap_or(None) {
                    match req {
                        SystemDataUpdaterRequest::SetSoftwareDistributionInfo {
                            info,
                            responder,
                        } => {
                            *chan.lock() = info.current_channel;
                            responder.send(CobaltStatus::Ok).unwrap();
                        }
                        _ => unreachable!(),
                    }
                }
            })
            .detach()
        })
        .serve_connection(svc_dir)
        .expect("serve_connection");

        fasync::Task::local(fs.collect()).detach();

        let mut notify_fut = current_channel_notifier.run().boxed();
        assert_eq!(exec.run_until_stalled(&mut notify_fut), Poll::Pending);

        assert_matches!(read_channel(&current_channel_path), Err(_));
        assert_eq!(channel.lock().as_ref().map(|s| s.as_str()), Some(""));

        exec.run_singlethreaded(target_channel_manager.update())
            .expect("channel update to succeed");
        exec.run_singlethreaded(current_channel_manager.update())
            .expect("current channel update to succeed");
        assert_eq!(exec.run_until_stalled(&mut notify_fut), Poll::Pending);

        assert_eq!(read_channel(&current_channel_path).unwrap(), "devhost");
        assert_eq!(channel.lock().as_ref().map(|s| s.as_str()), Some("devhost"));

        // Even if the current_channel is already known, it should be overwritten.
        write_channel(&target_channel_path, "different").unwrap();
        exec.run_singlethreaded(current_channel_manager.update())
            .expect("current channel update to succeed");
        assert_eq!(exec.run_until_stalled(&mut notify_fut), Poll::Pending);

        assert_eq!(read_channel(&current_channel_path).unwrap(), "different");
        assert_eq!(channel.lock().as_ref().map(|s| s.as_str()), Some("different"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_writes_channel() {
        let dir = tempfile::tempdir().unwrap();
        let target_channel_path = dir.path().join(TARGET_CHANNEL);

        let connector = RewriteServiceConnector::new("fuchsia-pkg://devhost/update/0");
        let channel_manager = TargetChannelManager::new(connector.clone(), dir.path());

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

        // If the update package changes, however, the file will be updated.
        connector.set("fuchsia-pkg://hello.world.fuchsia.com/update/0");
        channel_manager.update().await.expect("channel update to succeed");
        assert_eq!(channel_manager.get_target_channel(), Some("world".to_string()));
        assert_eq!(read_channel(&target_channel_path).unwrap(), "world");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_recovers_from_corrupt_data() {
        let dir = tempfile::tempdir().unwrap();
        let target_channel_path = dir.path().join(TARGET_CHANNEL);

        fs::write(&target_channel_path, r#"invalid json"#).unwrap();

        let connector = RewriteServiceConnector::new("fuchsia-pkg://a.b.c.fuchsia.com/update/0");
        let channel_manager = TargetChannelManager::new(connector, dir.path());

        assert!(read_channel(&target_channel_path).is_err());
        channel_manager.update().await.expect("channel update to succeed");
        assert_eq!(channel_manager.get_target_channel(), Some("b".to_string()));
        assert_eq!(read_channel(&target_channel_path).unwrap(), "b");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_set_target_channel() {
        let dir = tempfile::tempdir().unwrap();
        let target_channel_path = dir.path().join(TARGET_CHANNEL);

        let connector = RewriteServiceConnector::new("fuchsia-pkg://devhost/update/0");
        let channel_manager = TargetChannelManager::new(connector, dir.path());
        channel_manager.set_target_channel("target-channel".to_string()).await.unwrap();
        assert_eq!(channel_manager.get_target_channel(), Some("target-channel".to_string()));
        assert_eq!(read_channel(&target_channel_path).unwrap(), "target-channel");
        assert_eq!(
            mock_sysconfig::take_last_config(),
            Some(OtaUpdateChannelConfig::new("target-channel", "target-channel").unwrap())
        );
    }

    #[derive(Clone)]
    struct RewriteServiceConnector {
        target: Arc<Mutex<String>>,
    }

    impl RewriteServiceConnector {
        fn new(target: impl Into<String>) -> Self {
            Self { target: Arc::new(Mutex::new(target.into())) }
        }
        fn set(&self, target: impl Into<String>) {
            *self.target.lock() = target.into();
        }
    }

    impl ServiceConnect for RewriteServiceConnector {
        fn connect_to_service<S: DiscoverableService>(&self) -> Result<S::Proxy, anyhow::Error> {
            let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<S>().unwrap();
            assert_eq!(S::SERVICE_NAME, EngineMarker::SERVICE_NAME);
            let mut stream: EngineRequestStream = stream.cast_stream();

            let target = self.target.lock().clone();
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
            Ok(proxy)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_get_channel_list() {
        let dir = tempfile::tempdir().unwrap();
        let connector =
            RepoMgrServiceConnector { channels: vec!["some-channel", "target-channel"] };
        let channel_manager = TargetChannelManager::new(connector, dir.path());
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
        fn connect_to_service<S: DiscoverableService>(&self) -> Result<S::Proxy, anyhow::Error> {
            let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<S>().unwrap();
            assert_eq!(S::SERVICE_NAME, RepositoryManagerMarker::SERVICE_NAME);
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
                                        RepoUrl::new(format!("a.{}.c.fuchsia.com", channel))
                                            .unwrap(),
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
