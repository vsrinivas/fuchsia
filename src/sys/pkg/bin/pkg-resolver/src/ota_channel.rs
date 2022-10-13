// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::repository_manager::RepositoryManager,
    anyhow::{anyhow, format_err, Error},
    cobalt_sw_delivery_registry as metrics,
    fidl_contrib::protocol_connector::ProtocolSender,
    fidl_fuchsia_metrics::MetricEvent,
    fidl_fuchsia_pkg_rewrite_ext::Rule,
    fuchsia_cobalt_builders::MetricEventExt as _,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect::{self as inspect, Property as _, StringProperty},
    futures::{future::BoxFuture, prelude::*},
    tracing::info,
};

#[derive(Debug)]
pub enum ChannelSource {
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
    cobalt_sender: ProtocolSender<MetricEvent>,
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
    mut cobalt_sender: ProtocolSender<MetricEvent>,
) -> Result<Option<Rule>, Error>
where
    F: Fn() -> BoxFuture<'static, Result<String, Error>>,
{
    let res =
        create_rewrite_rule_for_ota_channel_impl(vbmeta_fn, channel_inspect_state, repo_manager)
            .await;
    cobalt_sender.send(
        MetricEvent::builder(
            metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_MIGRATED_METRIC_ID,
        )
        .with_event_codes(match &res {
            Ok(_) => {
                metrics::RepositoryManagerLoadRepositoryForChannelMigratedMetricDimensionResult::Success
            }
            Err(_) => {
                metrics::RepositoryManagerLoadRepositoryForChannelMigratedMetricDimensionResult::Failure
            }
        })
        .as_occurrence(1),
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
        Ok(tuf_config_name) => create_rewrite_rule_for_tuf_config_name(
            repo_manager,
            &channel_inspect_state,
            &tuf_config_name,
            ChannelSource::VbMeta,
        ),
        Err(e) => {
            info!("Unable to load channel from vbmeta: {:#}", anyhow!(e));
            Ok(None)
        }
    }
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
    let repo = fuchsia_url::RepositoryUrl::parse_host(tuf_config_name.to_string())
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
        let proxy = connect_to_protocol::<fidl_fuchsia_boot::ArgumentsMarker>()?;
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
mod test {
    use {
        super::*,
        crate::{
            repository_manager::RepositoryManagerBuilder,
            test_util::{get_mock_cobalt_sender, verify_cobalt_emits_event},
        },
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsRequest},
        fidl_fuchsia_pkg_ext::{RepositoryConfigBuilder, RepositoryConfigs, RepositoryKey},
        fuchsia_async as fasync,
        fuchsia_inspect::assert_data_tree,
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

    async fn setup_repo_mgr() -> (RepositoryManager, ChannelInspectState, fuchsia_inspect::Inspector)
    {
        // Set up inspect
        let inspector = fuchsia_inspect::Inspector::new();
        let channel_inspect_state =
            ChannelInspectState::new(inspector.root().create_child("omaha_channel"));

        // Set up static configs
        let vbmeta_repo_config = RepositoryConfigBuilder::new(
            fuchsia_url::RepositoryUrl::parse("fuchsia-pkg://repo-from-vbmeta").unwrap(),
        )
        .add_root_key(RepositoryKey::Ed25519(vec![0]))
        .build();
        let static_dir = crate::test_util::create_dir(vec![(
            "config",
            RepositoryConfigs::Version1(vec![vbmeta_repo_config]),
        )]);
        let dynamic_dir = tempfile::tempdir().unwrap();

        // Build repo mgr with static configs
        let repo_manager = RepositoryManagerBuilder::new_test(&dynamic_dir, Some("config"))
            .await
            .unwrap()
            .load_static_configs_dir(static_dir.path())
            .unwrap()
            .build();

        (repo_manager, channel_inspect_state, inspector)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_default_rule_from_ota_channel_in_vbmeta() {
        let (repo_manager, channel_inspect_state, inspector) = setup_repo_mgr().await;

        let (cobalt_sender, mut cobalt_receiver) = get_mock_cobalt_sender();

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
        assert_data_tree!(
            inspector,
            root: contains {
              omaha_channel: {
                tuf_config_name: "repo-from-vbmeta",
                source: format!("{:?}", Some(ChannelSource::VbMeta))
              }
            }
        );

        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_MIGRATED_METRIC_ID,
            metrics::RepositoryManagerLoadRepositoryForChannelMigratedMetricDimensionResult::Success,
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_default_rule_no_ota_channel() {
        let (repo_manager, channel_inspect_state, inspector) = setup_repo_mgr().await;

        let (cobalt_sender, mut cobalt_receiver) = get_mock_cobalt_sender();

        let res = create_rewrite_rule_for_ota_channel_impl_cobalt(
            failing_vbmeta_fn,
            &channel_inspect_state,
            &repo_manager,
            cobalt_sender,
        )
        .await;

        assert_matches::assert_matches!(res, Ok(None));
        assert_data_tree!(
            inspector,
            root: contains {
              omaha_channel: {
                tuf_config_name: format!("{:?}", Option::<String>::None),
                source: format!("{:?}", Option::<String>::None)

              }
            }
        );

        verify_cobalt_emits_event(
            &mut cobalt_receiver,
            metrics::REPOSITORY_MANAGER_LOAD_REPOSITORY_FOR_CHANNEL_MIGRATED_METRIC_ID,
            metrics::RepositoryManagerLoadRepositoryForChannelMigratedMetricDimensionResult::Success,
        );
    }
}
