// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::connect::*;
use anyhow::anyhow;
use fidl_fuchsia_boot::ArgumentsMarker;
use fidl_fuchsia_cobalt::{
    SoftwareDistributionInfo, Status as CobaltStatus, SystemDataUpdaterMarker,
    SystemDataUpdaterProxy,
};
use fidl_fuchsia_pkg::RepositoryManagerMarker;
use fidl_fuchsia_pkg_ext::RepositoryConfig;
use fuchsia_async as fasync;
use fuchsia_url::AbsolutePackageUrl;
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::convert::TryInto;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::time::Duration;
use thiserror::Error;
use tracing::{error, info, warn};

static CHANNEL_PACKAGE_MAP: &str = "channel_package_map.json";

pub async fn build_current_channel_manager_and_notifier<S: ServiceConnect>(
    service_connector: S,
) -> Result<(CurrentChannelManager, CurrentChannelNotifier<S>), anyhow::Error> {
    let (current_channel, current_realm) = if let (Some(channel), Some(realm)) =
        lookup_channel_and_realm_from_vbmeta(&service_connector).await.unwrap_or_else(|e| {
            warn!("Failed to read current_channel from vbmeta: {:#}", anyhow!(e));
            (None, None)
        }) {
        (channel, Some(realm))
    } else {
        (String::new(), None)
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

            info!("calling cobalt.SetSoftwareDistributionInfo(\"{:?}\")", distribution_info);

            match cobalt.set_software_distribution_info(distribution_info).await {
                Ok(CobaltStatus::Ok) => {
                    return;
                }
                Ok(CobaltStatus::EventTooBig) => {
                    warn!(
                        "cobalt.SetSoftwareDistributionInfo returned Status.EVENT_TOO_BIG, retrying"
                    );
                }
                Ok(status) => {
                    // Not much we can do about the other status codes but log.
                    error!(
                        "cobalt.SetSoftwareDistributionInfo returned non-OK status: {:?}",
                        status
                    );
                    return;
                }
                Err(err) => {
                    // channel broken, so log the error and reconnect.
                    warn!(
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
                    error!("error connecting to cobalt: {:#}", anyhow!(err));
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
    target_channel: Mutex<Option<String>>,
    channel_package_map: HashMap<String, AbsolutePackageUrl>,
}

impl<S: ServiceConnect> TargetChannelManager<S> {
    /// Create a new |TargetChannelManager|.
    ///
    /// Arguments:
    /// * `service_connector` - used to connect to fuchsia.pkg.RepositoryManager and
    ///   fuchsia.boot.ArgumentsMarker.
    /// * `config_dir` - directory containing immutable configuration, usually /config/data.
    pub fn new(service_connector: S, config_dir: impl Into<PathBuf>) -> Self {
        let target_channel = Mutex::new(None);
        let mut config_path = config_dir.into();
        config_path.push(CHANNEL_PACKAGE_MAP);
        let channel_package_map = read_channel_mappings(&config_path).unwrap_or_else(|err| {
            warn!("Failed to load {}: {:?}", CHANNEL_PACKAGE_MAP, err);
            HashMap::new()
        });

        Self { service_connector, target_channel, channel_package_map }
    }

    /// Fetch the target channel from vbmeta, if one is present.
    /// Otherwise, it will set the channel to an empty string.
    pub async fn update(&self) -> Result<(), anyhow::Error> {
        let (target_channel, _) =
            lookup_channel_and_realm_from_vbmeta(&self.service_connector).await?;

        // If the vbmeta has a channel, ensure our target channel matches and return.
        if let Some(channel) = target_channel {
            self.set_target_channel(channel);
            return Ok(());
        }

        // Otherwise, set the target channel to "".
        self.set_target_channel("".to_owned());
        Ok(())
    }

    pub fn get_target_channel(&self) -> Option<String> {
        self.target_channel.lock().clone()
    }

    /// Returns the update URL for the current target channel, if the channel exists and is not
    /// empty.
    pub fn get_target_channel_update_url(&self) -> Option<String> {
        let target_channel = self.get_target_channel()?;
        match self.channel_package_map.get(&target_channel) {
            Some(url) => Some(url.to_string()),
            None => {
                if target_channel.is_empty() {
                    None
                } else {
                    Some(format!("fuchsia-pkg://{}/update", target_channel))
                }
            }
        }
    }

    pub fn set_target_channel(&self, channel: String) {
        *self.target_channel.lock() = Some(channel);
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

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
pub enum ChannelPackageMap {
    #[serde(rename = "1")]
    Version1(Vec<ChannelPackagePair>),
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct ChannelPackagePair {
    channel: String,
    package: AbsolutePackageUrl,
}

fn read_channel_mappings(
    p: impl AsRef<Path>,
) -> Result<HashMap<String, AbsolutePackageUrl>, Error> {
    let f = fs::File::open(p.as_ref())?;
    let mut result = HashMap::new();
    match serde_json::from_reader(io::BufReader::new(f))? {
        ChannelPackageMap::Version1(items) => {
            for item in items.into_iter() {
                if let Some(old_pkg) = result.insert(item.channel.clone(), item.package.clone()) {
                    error!(
                        "Duplicate update package definition for channel {}: {} and {}.",
                        item.channel, item.package, old_pkg
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
    use fuchsia_async::DurationExt;
    use fuchsia_component::server::ServiceFs;
    use fuchsia_url::RepositoryUrl;
    use fuchsia_zircon::DurationNum;
    use futures::prelude::*;
    use futures::task::Poll;
    use futures::{future::FutureExt, stream::StreamExt};
    use parking_lot::Mutex;
    use std::sync::Arc;

    fn serve_ota_channel_arguments(
        mut stream: ArgumentsRequestStream,
        channel: Option<&'static str>,
        realm: Option<&'static str>,
    ) -> fasync::Task<()> {
        fasync::Task::local(async move {
            while let Some(req) = stream.try_next().await.unwrap_or(None) {
                match req {
                    ArgumentsRequest::GetStrings { keys, responder } => {
                        assert_eq!(keys, vec!["ota_channel", "omaha_app_id"]);
                        let response = [channel.clone(), realm.clone()];
                        responder.send(&mut response.iter().map(|v| *v)).expect("send ok");
                    }
                    _ => unreachable!(),
                }
            }
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_current_channel_manager_and_notifier_uses_vbmeta() {
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
                    }
                }
            })
            .detach()
        })
        .add_fidl_service(move |stream: ArgumentsRequestStream| {
            serve_ota_channel_arguments(stream, Some("stable"), Some("sample-realm")).detach()
        })
        .serve_connection(svc_dir)
        .expect("serve_connection");

        fasync::Task::local(fs.collect()).detach();
        let (m, c) = build_current_channel_manager_and_notifier(connector).await.unwrap();

        assert_eq!(&m.channel, "stable");
        assert_eq!(&c.channel, "stable");
        assert_eq!(c.realm.as_deref(), Some("sample-realm"));

        c.run().await;

        let lock = channel_and_realm.lock();
        assert_eq!(lock.0.as_deref(), Some("stable"));
        assert_eq!(lock.1.as_deref(), Some("sample-realm"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_current_channel_manager_and_notifier_uses_fallback() {
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
                    }
                }
            })
            .detach()
        })
        .add_fidl_service(move |stream: ArgumentsRequestStream| {
            serve_ota_channel_arguments(stream, None, None).detach()
        })
        .serve_connection(svc_dir)
        .expect("serve_connection");

        fasync::Task::local(fs.collect()).detach();
        let (m, c) = build_current_channel_manager_and_notifier(connector).await.unwrap();

        assert_eq!(m.channel, "");
        assert_eq!(c.channel, "");
        assert_eq!(c.realm, None);

        c.run().await;

        let lock = channel_and_realm.lock();
        assert_eq!(lock.0.as_deref(), Some(""));
        assert_eq!(lock.1.as_deref(), None);
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
                                    }
                                    }
                                })
                                .detach();
                            }
                            ArgumentsMarker::PROTOCOL_NAME => {
                                serve_ota_channel_arguments(
                                    stream.cast_stream(),
                                    Some("stable"),
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

    async fn check_target_channel_manager_remembers_channel(
        ota_channel: Option<String>,
        initial_channel: String,
    ) {
        let dir = tempfile::tempdir().unwrap();

        let connector = ArgumentsServiceConnector::new(ota_channel.clone());
        let channel_manager = TargetChannelManager::new(connector.clone(), dir.path());

        // Starts with expected initial channel
        channel_manager.update().await.expect("channel update to succeed");
        assert_eq!(channel_manager.get_target_channel(), Some(initial_channel.clone()));

        // If the update package changes, or our vbmeta changes, the target_channel will be
        // updated.
        connector.set(Some("world".to_owned()));
        channel_manager.update().await.expect("channel update to succeed");
        assert_eq!(channel_manager.get_target_channel(), Some("world".to_string()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_remembers_channel_with_vbmeta() {
        check_target_channel_manager_remembers_channel(
            Some("devhost".to_string()),
            "devhost".to_string(),
        )
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_remembers_channel_with_fallback() {
        check_target_channel_manager_remembers_channel(None, String::new()).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_set_target_channel() {
        let dir = tempfile::tempdir().unwrap();

        let connector = ArgumentsServiceConnector::new(Some("not-target-channel".to_string()));
        let channel_manager = TargetChannelManager::new(connector, dir.path());
        channel_manager.set_target_channel("target-channel".to_string());
        assert_eq!(channel_manager.get_target_channel(), Some("target-channel".to_string()));
    }

    async fn check_target_channel_manager_update(
        ota_channel: Option<String>,
        expected_channel: String,
    ) {
        let dir = tempfile::tempdir().unwrap();

        let connector = ArgumentsServiceConnector::new(ota_channel.clone());
        let channel_manager = TargetChannelManager::new(connector, dir.path());
        channel_manager.update().await.unwrap();
        assert_eq!(channel_manager.get_target_channel(), Some(expected_channel));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_update_uses_vbmeta() {
        check_target_channel_manager_update(
            Some("not-devhost".to_string()),
            "not-devhost".to_string(),
        )
        .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_channel_manager_update_uses_fallback() {
        check_target_channel_manager_update(None, String::new()).await
    }

    #[derive(Clone)]
    struct ArgumentsServiceConnector {
        ota_channel: Arc<Mutex<Option<String>>>,
    }

    impl ArgumentsServiceConnector {
        fn new<'a>(ota_channel: Option<String>) -> Self {
            Self { ota_channel: Arc::new(Mutex::new(ota_channel)) }
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

        let channel_manager = TargetChannelManager::new(connector, dir.path());
        assert_eq!(channel_manager.get_target_channel_update_url(), None);
        channel_manager.set_target_channel("first".to_owned());
        assert_eq!(
            channel_manager.get_target_channel_update_url(),
            Some("fuchsia-pkg://asdfghjkl.example.com/update".to_owned())
        );

        channel_manager.set_target_channel("does_not_exist".to_owned());
        assert_eq!(
            channel_manager.get_target_channel_update_url(),
            Some("fuchsia-pkg://does_not_exist/update".to_owned())
        );

        channel_manager.set_target_channel("qwertyuiop.example.com".to_owned());
        assert_eq!(
            channel_manager.get_target_channel_update_url(),
            Some("fuchsia-pkg://qwertyuiop.example.com/update".to_owned())
        );

        channel_manager.set_target_channel(String::new());
        assert_eq!(channel_manager.get_target_channel_update_url(), None);
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

        let channel_manager = TargetChannelManager::new(connector, dir.path());
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
                                        RepositoryUrl::parse_host(channel.to_string()).unwrap(),
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
