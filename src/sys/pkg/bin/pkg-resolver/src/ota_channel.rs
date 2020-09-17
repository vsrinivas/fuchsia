// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::repository_manager::RepositoryManager,
    anyhow::{anyhow, format_err, Error},
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_pkg_rewrite_ext::Rule,
    fuchsia_cobalt::CobaltSender,
    fuchsia_component::client::connect_to_service,
    fuchsia_inspect::{self as inspect, Property as _, StringProperty},
    fuchsia_syslog::{self, fx_log_info},
    fuchsia_url::pkg_url::RepoUrl,
    futures::{future::BoxFuture, prelude::*},
};

#[cfg(not(test))]
use sysconfig_client::channel::read_channel_config;

#[cfg(test)]
use sysconfig_mock::read_channel_config;

#[derive(Debug)]
pub enum ChannelSource {
    SysConfig,
    VbMeta,
}

pub struct ChannelInspectState {
    tuf_config_name: StringProperty,
    source: StringProperty,
    _node: inspect::Node,
}

impl ChannelInspectState {
    pub fn new(node: inspect::Node) -> Self {
        Self {
            tuf_config_name: node
                .create_string("tuf_config_name", format!("{:?}", Option::<String>::None)),
            source: node.create_string("source", format!("{:?}", Option::<String>::None)),
            _node: node,
        }
    }
}

pub async fn create_rewrite_rule_for_ota_channel(
    channel_inspect_state: &ChannelInspectState,
    repo_manager: &RepositoryManager,
    cobalt_sender: CobaltSender,
) -> Result<Option<Rule>, Error> {
    create_rewrite_rule_for_ota_channel_impl_cobalt(
        get_tuf_config_name_from_vbmeta,
        channel_inspect_state,
        repo_manager,
        cobalt_sender,
    )
    .await
}

async fn create_rewrite_rule_for_ota_channel_impl_cobalt<F>(
    vbmeta_fn: F,
    channel_inspect_state: &ChannelInspectState,
    repo_manager: &RepositoryManager,
    mut cobalt_sender: CobaltSender,
) -> Result<Option<Rule>, Error>
where
    F: Fn() -> BoxFuture<'static, Result<String, Error>>,
{
    let res =
        create_rewrite_rule_for_ota_channel_impl(vbmeta_fn, channel_inspect_state, repo_manager)
            .await;
    cobalt_sender.log_event_count(
        metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_METRIC_ID,
        match &res {
            Ok(_) => {
                metrics::RepositoryManagerLoadRepositoryForChannelMetricDimensionResult::Success
            }
            Err(_) => {
                metrics::RepositoryManagerLoadRepositoryForChannelMetricDimensionResult::Failure
            }
        },
        0,
        1,
    );
    res
}

async fn create_rewrite_rule_for_ota_channel_impl<F>(
    vbmeta_fn: F,
    channel_inspect_state: &ChannelInspectState,
    repo_manager: &RepositoryManager,
) -> Result<Option<Rule>, Error>
where
    F: Fn() -> BoxFuture<'static, Result<String, Error>>,
{
    // First check if channel info is in vbmeta
    match vbmeta_fn().await {
        Ok(tuf_config_name) => {
            return create_rewrite_rule_for_tuf_config_name(
                repo_manager,
                &channel_inspect_state,
                &tuf_config_name,
                ChannelSource::VbMeta,
            );
        }
        Err(e) => {
            fx_log_info!("Unable to load channel from vbmeta: {:#}", anyhow!(e));
        }
    };

    // If we don't find channel info in vbmeta, try looking sysconfig
    let channel_config = match read_channel_config().await {
        Ok(channel_config) => channel_config,
        Err(e) => {
            fx_log_info!("Unable to load channel from sysconfig: {:#}", anyhow!(e));
            return Ok(None);
        }
    };
    create_rewrite_rule_for_tuf_config_name(
        repo_manager,
        &channel_inspect_state,
        channel_config.tuf_config_name(),
        ChannelSource::SysConfig,
    )
}

fn create_rewrite_rule_for_tuf_config_name(
    repo_manager: &RepositoryManager,
    channel_inspect_state: &ChannelInspectState,
    tuf_config_name: &str,
    source: ChannelSource,
) -> Result<Option<Rule>, Error> {
    // tuf_config_name could either be the full repo hostname or the name of the channel.
    // In order to (1) verify the corresponding repo is actually registered and (2) obtain
    // full repo name if tuf_config_name is a channel, we will do the following:
    let repo = RepoUrl::parse(&format!("fuchsia-pkg://{}", tuf_config_name))
        .ok()
        .and_then(|url| repo_manager.get(&url))
        .or_else(|| repo_manager.get_repo_for_channel(tuf_config_name))
        .ok_or_else(|| {
            format_err!("Unable to find repo for tuf_config_name: {:?}", tuf_config_name)
        })?;

    let rule = Rule::new("fuchsia.com", repo.repo_url().host(), "/", "/")?;
    channel_inspect_state.tuf_config_name.set(&tuf_config_name);
    channel_inspect_state.source.set(&format!("{:?}", Some(source)));

    Ok(Some(rule))
}

fn get_tuf_config_name_from_vbmeta() -> BoxFuture<'static, Result<String, Error>> {
    async move {
        let proxy = connect_to_service::<fidl_fuchsia_boot::ArgumentsMarker>()?;
        get_tuf_config_name_from_vbmeta_impl(proxy).await
    }
    .boxed()
}

async fn get_tuf_config_name_from_vbmeta_impl(
    proxy: fidl_fuchsia_boot::ArgumentsProxy,
) -> Result<String, Error> {
    let repo_config = proxy.get_string("tuf_repo_config").await?;
    if let Some(hostname) = repo_config {
        Ok(hostname)
    } else {
        Err(format_err!("Could not find tuf_repo_config in vbmeta"))
    }
}

#[cfg(test)]
mod sysconfig_mock {
    use {
        std::{
            cell::RefCell,
            sync::atomic::{AtomicU8, Ordering},
        },
        sysconfig_client::channel::{ChannelConfigError, OtaUpdateChannelConfig},
    };

    thread_local! {
        static MOCK_RESULT: RefCell<Result<OtaUpdateChannelConfig, ChannelConfigError>> =
            RefCell::new(Err(ChannelConfigError::Magic(0)));
        static READ_COUNT: AtomicU8 = AtomicU8::new(0);
    }

    pub(super) async fn read_channel_config() -> Result<OtaUpdateChannelConfig, ChannelConfigError>
    {
        if READ_COUNT.with(|i| i.fetch_add(1, Ordering::SeqCst)) > 0 {
            panic!("Should only call read_channel_config once");
        }
        MOCK_RESULT.with(|result| result.replace(Err(ChannelConfigError::Magic(0))))
    }

    pub(super) fn set_read_channel_config_result(
        new_result: Result<OtaUpdateChannelConfig, ChannelConfigError>,
    ) {
        READ_COUNT.with(|i| i.store(0, Ordering::SeqCst));
        MOCK_RESULT.with(|result| *result.borrow_mut() = new_result);
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::repository_manager::RepositoryManagerBuilder,
        cobalt_client::traits::AsEventCode as _,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsRequest},
        fidl_fuchsia_cobalt::{CobaltEvent, CountEvent, EventPayload},
        fidl_fuchsia_pkg_ext::{RepositoryConfigBuilder, RepositoryConfigs, RepositoryKey},
        fuchsia_async as fasync,
        fuchsia_inspect::assert_inspect_tree,
        futures::channel::mpsc,
        sysconfig_client::channel::OtaUpdateChannelConfig,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_get_tuf_config_name_from_vbmeta() {
        // Create a fake service that responds to ArgumentsRequests
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        fasync::Task::spawn(async move {
            match stream.next().await.unwrap() {
                Ok(ArgumentsRequest::GetString { key, responder }) => {
                    assert_eq!(key, "tuf_repo_config", "Unexpected GetString request: {}", key);
                    responder.send(Some("test-repo-config")).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .detach();

        let tuf_config = get_tuf_config_name_from_vbmeta_impl(proxy).await.unwrap();

        assert_eq!(tuf_config, "test-repo-config");
    }

    fn succeeding_vbmeta_fn() -> BoxFuture<'static, Result<String, Error>> {
        async move { Ok("repo-from-vbmeta".to_string()) }.boxed()
    }

    fn failing_vbmeta_fn() -> BoxFuture<'static, Result<String, Error>> {
        async move { anyhow::bail!("FAILURE") }.boxed()
    }

    fn verify_cobalt_emits_event(
        mut cobalt_receiver: mpsc::Receiver<CobaltEvent>,
        metric_id: u32,
        expected_event_codes: Vec<u32>,
    ) {
        assert_eq!(
            cobalt_receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id,
                event_codes: expected_event_codes,
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 1
                }),
            }
        );
    }

    fn setup_repo_mgr() -> (RepositoryManager, ChannelInspectState, fuchsia_inspect::Inspector) {
        // Set up inspect
        let inspector = fuchsia_inspect::Inspector::new();
        let channel_inspect_state =
            ChannelInspectState::new(inspector.root().create_child("omaha_channel"));

        // Set up static configs
        let sysconfig_repo_config = RepositoryConfigBuilder::new(
            RepoUrl::parse("fuchsia-pkg://a.channel-from-sysconfig.bcde.fuchsia.com").unwrap(),
        )
        .add_root_key(RepositoryKey::Ed25519(vec![0]))
        .build();
        let vbmeta_repo_config =
            RepositoryConfigBuilder::new(RepoUrl::parse("fuchsia-pkg://repo-from-vbmeta").unwrap())
                .add_root_key(RepositoryKey::Ed25519(vec![0]))
                .build();
        let static_dir = crate::test_util::create_dir(vec![(
            "config",
            RepositoryConfigs::Version1(vec![sysconfig_repo_config, vbmeta_repo_config]),
        )]);
        let dynamic_dir = tempfile::tempdir().unwrap();
        let dynamic_configs_path = dynamic_dir.path().join("config");

        // Build repo mgr with static configs
        let repo_manager = RepositoryManagerBuilder::new_test(Some(&dynamic_configs_path))
            .unwrap()
            .load_static_configs_dir(static_dir.path())
            .unwrap()
            .build();

        (repo_manager, channel_inspect_state, inspector)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_default_rule_from_ota_channel_in_vbmeta() {
        let (repo_manager, channel_inspect_state, inspector) = setup_repo_mgr();
        sysconfig_mock::set_read_channel_config_result(OtaUpdateChannelConfig::new(
            "channel-from-sysconfig-ignore",
            "channel-from-sysconfig",
        ));

        // Set up Cobalt.
        let (sender, cobalt_receiver) = futures::channel::mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);

        let res = create_rewrite_rule_for_ota_channel_impl_cobalt(
            succeeding_vbmeta_fn,
            &channel_inspect_state,
            &repo_manager,
            cobalt_sender,
        )
        .await;

        assert_eq!(
            res.unwrap().unwrap(),
            Rule::new("fuchsia.com", "repo-from-vbmeta", "/", "/").unwrap()
        );
        assert_inspect_tree!(
            inspector,
            root: contains {
              omaha_channel: {
                tuf_config_name: "repo-from-vbmeta",
                source: format!("{:?}", Some(ChannelSource::VbMeta))
              }
            }
        );

        verify_cobalt_emits_event(
            cobalt_receiver,
            metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_METRIC_ID,
            vec![metrics::RepositoryManagerLoadRepositoryForChannelMetricDimensionResult::Success
                .as_event_code()],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_default_rule_from_ota_channel_in_sysconfig() {
        let (repo_manager, channel_inspect_state, inspector) = setup_repo_mgr();
        sysconfig_mock::set_read_channel_config_result(OtaUpdateChannelConfig::new(
            "channel-from-sysconfig-ignore",
            "channel-from-sysconfig",
        ));

        // Set up Cobalt.
        let (sender, cobalt_receiver) = futures::channel::mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);

        let res = create_rewrite_rule_for_ota_channel_impl_cobalt(
            failing_vbmeta_fn,
            &channel_inspect_state,
            &repo_manager,
            cobalt_sender,
        )
        .await;

        assert_eq!(
            res.unwrap().unwrap(),
            Rule::new("fuchsia.com", "a.channel-from-sysconfig.bcde.fuchsia.com", "/", "/")
                .unwrap()
        );
        assert_inspect_tree!(
            inspector,
            root: contains {
              omaha_channel: {
                tuf_config_name: "channel-from-sysconfig",
                source: format!("{:?}", Some(ChannelSource::SysConfig))
              }
            }
        );

        verify_cobalt_emits_event(
            cobalt_receiver,
            metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_METRIC_ID,
            vec![metrics::RepositoryManagerLoadRepositoryForChannelMetricDimensionResult::Success
                .as_event_code()],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_default_rule_no_ota_channel() {
        let (repo_manager, channel_inspect_state, inspector) = setup_repo_mgr();

        // Set up Cobalt.
        let (sender, cobalt_receiver) = futures::channel::mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);

        // Since we didn't do sysconfig_mock::set_read_channel_config_result, the result
        // defaults to an error state
        let res = create_rewrite_rule_for_ota_channel_impl_cobalt(
            failing_vbmeta_fn,
            &channel_inspect_state,
            &repo_manager,
            cobalt_sender,
        )
        .await;

        matches::assert_matches!(res, Ok(None));
        assert_inspect_tree!(
            inspector,
            root: contains {
              omaha_channel: {
                tuf_config_name: format!("{:?}", Option::<String>::None),
                source: format!("{:?}", Option::<String>::None)

              }
            }
        );

        verify_cobalt_emits_event(
            cobalt_receiver,
            metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_METRIC_ID,
            vec![metrics::RepositoryManagerLoadRepositoryForChannelMetricDimensionResult::Success
                .as_event_code()],
        );
    }
}
