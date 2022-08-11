// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use anyhow::{Context, Error};
use fidl_fuchsia_metrics::MetricEventLoggerFactoryMarker;
use fidl_fuchsia_stash::StoreMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_fs::OpenFlags;
use fuchsia_inspect::{self as inspect, component};
use fuchsia_syslog::{self as syslog, fx_log_info};
use futures::lock::Mutex;
use lazy_static::lazy_static;
use settings::agent::BlueprintHandle as AgentBlueprintHandle;
use settings::base::get_default_interfaces;
use settings::config::base::{get_default_agent_types, AgentType};
use settings::config::default_settings::DefaultSetting;
use settings::handler::setting_proxy_inspect_info::SettingProxyInspectInfo;
use settings::inspect::listener_logger::ListenerInspectLogger;
use settings::AgentConfiguration;
use settings::EnabledInterfacesConfiguration;
use settings::EnvironmentBuilder;
use settings::ServiceConfiguration;
use settings::ServiceFlags;
use settings_storage::stash_logger::StashInspectLoggerHandle;
use settings_storage::storage_factory::StashDeviceStorageFactory;
use std::path::Path;
use std::sync::Arc;

const STASH_IDENTITY: &str = "settings_service";

lazy_static! {
    // TODO(fxb/93842): replace with a dependency injected value instead of a static.
    static ref SETTING_PROXY_INSPECT_INFO: SettingProxyInspectInfo =
        SettingProxyInspectInfo::new(component::inspector().root());

    static ref LISTENER_INSPECT_LOGGER: Arc<Mutex<ListenerInspectLogger>> =
        Arc::new(Mutex::new(ListenerInspectLogger::new()));
}

fn main() -> Result<(), Error> {
    let executor = fasync::LocalExecutor::new()?;

    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    // Serve stats about inspect in a lazy node.
    let inspector = component::inspector();
    let node = inspect::stats::Node::new(inspector, inspector.root());
    inspector.root().record(node.take());

    let default_enabled_interfaces_configuration =
        EnabledInterfacesConfiguration::with_interfaces(get_default_interfaces());

    let enabled_interface_configuration = DefaultSetting::new(
        Some(default_enabled_interfaces_configuration),
        "/config/data/interface_configuration.json",
    )
    .load_default_value()
    .expect("invalid default enabled interface configuration")
    .expect("no default enabled interfaces configuration");

    let flags =
        DefaultSetting::new(Some(ServiceFlags::default()), "/config/data/service_flags.json")
            .load_default_value()
            .expect("invalid service flag configuration")
            .expect("no default service flags");

    // Temporary solution for FEMU to have an agent config without camera agent.
    let agent_config = "/config/data/agent_configuration.json";
    let board_agent_config = "/config/data/board_agent_configuration.json";
    let agent_configuration_file_path =
        if Path::new(board_agent_config).exists() { board_agent_config } else { agent_config };

    let agent_types = DefaultSetting::new(
        Some(AgentConfiguration { agent_types: get_default_agent_types() }),
        agent_configuration_file_path,
    )
    .load_default_value()
    .expect("invalid default agent configuration")
    .expect("no default agent types");

    let configuration =
        ServiceConfiguration::from(agent_types, enabled_interface_configuration, flags);

    let store_proxy = connect_to_protocol::<StoreMarker>().expect("failed to connect to stash");
    store_proxy.identify(STASH_IDENTITY).expect("should identify");
    let storage_factory =
        StashDeviceStorageFactory::new(store_proxy.clone(), StashInspectLoggerHandle::new().logger);

    let storage_dir = fuchsia_fs::directory::open_in_namespace(
        "/data/storage",
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE | OpenFlags::CREATE,
    )
    .context("unable to open data dir")?;
    // EnvironmentBuilder::spawn returns a future that can be awaited for the
    // result of the startup. Since main is a synchronous function, we cannot
    // block here and therefore continue without waiting for the result.
    EnvironmentBuilder::new(Arc::new(storage_factory))
        .configuration(configuration)
        .agent_mapping(<AgentBlueprintHandle as From<AgentType>>::from)
        .setting_proxy_inspect_info(
            SETTING_PROXY_INSPECT_INFO.node(),
            LISTENER_INSPECT_LOGGER.clone(),
        )
        .storage_dir(storage_dir)
        .store_proxy(store_proxy)
        .metric_event_logger_factory_proxy(
            connect_to_protocol::<MetricEventLoggerFactoryMarker>().ok(),
        )
        .spawn(executor)
        .context("Failed to spawn environment for setui")
}
