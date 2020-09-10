// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{fidl::State, policy::PolicyConfig};
use chrono::{DateTime, Utc};
use fuchsia_inspect::{Node, Property, StringProperty};
use omaha_client::{
    common::{App, ProtocolState, UpdateCheckSchedule},
    configuration::{Config, Updater},
    protocol::request::OS,
    state_machine::{update_check, UpdateCheckError},
};
use std::collections::VecDeque;
use std::time::SystemTime;

pub struct ConfigurationNode {
    _node: Node,
    updater: UpdaterNode,
    os: OsNode,
    omaha: OmahaNode,
}

impl ConfigurationNode {
    pub fn new(configuration_node: Node) -> Self {
        ConfigurationNode {
            updater: UpdaterNode::new(configuration_node.create_child("updater")),
            os: OsNode::new(configuration_node.create_child("os")),
            omaha: OmahaNode::new(configuration_node.create_child("omaha")),
            _node: configuration_node,
        }
    }

    pub fn set(&self, config: &Config) {
        self.updater.set(&config.updater);
        self.os.set(&config.os);
        self.omaha.set(&config.service_url);
    }
}

struct UpdaterNode {
    _node: Node,
    name: StringProperty,
    version: StringProperty,
}

impl UpdaterNode {
    fn new(updater_node: Node) -> Self {
        UpdaterNode {
            name: updater_node.create_string("name", ""),
            version: updater_node.create_string("version", ""),
            _node: updater_node,
        }
    }

    fn set(&self, updater: &Updater) {
        self.name.set(&updater.name);
        self.version.set(&updater.version.to_string());
    }
}

struct OsNode {
    _node: Node,
    platform: StringProperty,
    version: StringProperty,
    service_pack: StringProperty,
    arch: StringProperty,
}

impl OsNode {
    fn new(os_node: Node) -> Self {
        OsNode {
            platform: os_node.create_string("platform", ""),
            version: os_node.create_string("version", ""),
            service_pack: os_node.create_string("service_pack", ""),
            arch: os_node.create_string("arch", ""),
            _node: os_node,
        }
    }

    fn set(&self, os: &OS) {
        self.platform.set(&os.platform);
        self.version.set(&os.version);
        self.service_pack.set(&os.service_pack);
        self.arch.set(&os.arch);
    }
}

struct OmahaNode {
    _node: Node,
    service_url: StringProperty,
}

impl OmahaNode {
    fn new(omaha_node: Node) -> Self {
        OmahaNode { service_url: omaha_node.create_string("service_url", ""), _node: omaha_node }
    }

    fn set(&self, service_url: &str) {
        self.service_url.set(service_url);
    }
}

pub struct AppsNode {
    _node: Node,
    apps: StringProperty,
}

impl AppsNode {
    pub fn new(apps_node: Node) -> Self {
        AppsNode { apps: apps_node.create_string("apps", ""), _node: apps_node }
    }

    pub fn set(&self, apps: &[App]) {
        self.apps.set(&format!("{:?}", apps));
    }
}

pub struct StateNode {
    _node: Node,
    state: StringProperty,
}

impl StateNode {
    pub fn new(state_node: Node) -> Self {
        StateNode { state: state_node.create_string("state", ""), _node: state_node }
    }

    pub fn set(&self, state: &State) {
        self.state.set(&format!("{:?}", state));
    }
}

pub struct ScheduleNode {
    _node: Node,
    schedule: StringProperty,
}

impl ScheduleNode {
    pub fn new(schedule_node: Node) -> Self {
        ScheduleNode { schedule: schedule_node.create_string("schedule", ""), _node: schedule_node }
    }

    pub fn set(&self, schedule: &UpdateCheckSchedule) {
        self.schedule.set(&format!("{:?}", schedule));
    }
}

pub struct ProtocolStateNode {
    _node: Node,
    protocol_state: StringProperty,
}

impl ProtocolStateNode {
    pub fn new(protocol_state_node: Node) -> Self {
        ProtocolStateNode {
            protocol_state: protocol_state_node.create_string("protocol_state", ""),
            _node: protocol_state_node,
        }
    }

    pub fn set(&self, protocol_state: &ProtocolState) {
        self.protocol_state.set(&format!("{:?}", protocol_state));
    }
}

pub struct LastResultsNode {
    node: Node,
    last_results: VecDeque<Node>,
}

impl LastResultsNode {
    pub fn new(last_results_node: Node) -> Self {
        LastResultsNode { node: last_results_node, last_results: VecDeque::new() }
    }

    pub fn add_result(
        &mut self,
        start_time: SystemTime,
        result: &Result<update_check::Response, UpdateCheckError>,
    ) {
        // Use formatted time string as the name of the node.
        let time_str = DateTime::<Utc>::from(start_time).to_rfc3339();
        let result_node = self.node.create_child(&time_str);

        result_node.record_string("result", format!("{:#?}", result));

        self.last_results.push_back(result_node);
        // Dropping the string property will remove it from inspect.
        if self.last_results.len() > 10 {
            self.last_results.pop_front();
        }
    }
}

pub struct PolicyConfigNode {
    _node: Node,
}

impl PolicyConfigNode {
    pub fn new(node: Node, policy_config: &PolicyConfig) -> Self {
        node.record_uint("periodic_interval", policy_config.periodic_interval.as_secs());
        node.record_uint("startup_delay", policy_config.startup_delay.as_secs());
        node.record_uint("retry_delay", policy_config.retry_delay.as_secs());
        node.record_bool("allow_reboot_when_idle", policy_config.allow_reboot_when_idle);
        PolicyConfigNode { _node: node }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::configuration::get_config;
    use fuchsia_async as fasync;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use omaha_client::{common::UserCounting, protocol::Cohort, state_machine};
    use std::time::Duration;

    #[fasync::run_singlethreaded(test)]
    async fn test_configuration_node() {
        let inspector = Inspector::new();
        let node = ConfigurationNode::new(inspector.root().create_child("configuration"));
        node.set(&get_config("0.1.2").await);

        assert_inspect_tree!(
            inspector,
            root: {
                configuration: {
                    updater: {
                        name: "Fuchsia",
                        version: "0.0.1.0",
                    },
                    os: {
                        platform: "Fuchsia",
                        version: "0.1.2",
                        service_pack: "",
                        arch: std::env::consts::ARCH,
                    },
                    omaha: {
                        service_url: "https://clients2.google.com/service/update2/fuchsia/json",
                    },
                }
            }
        );
    }

    #[test]
    fn test_apps_node() {
        let inspector = Inspector::new();
        let node = AppsNode::new(inspector.root().create_child("apps"));
        let apps = vec![
            App::builder("id", [1, 0]).build(),
            App::builder("id_2", [1, 2, 4]).with_cohort(Cohort::new("cohort")).build(),
        ];
        node.set(&apps);

        assert_inspect_tree!(
            inspector,
            root: {
                apps: {
                    apps: format!("{:?}", apps),
                }
            }
        );
    }

    #[test]
    fn test_state_node() {
        let inspector = Inspector::new();
        let node = StateNode::new(inspector.root().create_child("state"));
        let state = State {
            manager_state: state_machine::State::CheckingForUpdates,
            version_available: Some("1.2.3.4".to_string()),
            install_progress: None,
        };
        node.set(&state);

        assert_inspect_tree!(
            inspector,
            root: {
                state: {
                    state: format!("{:?}", state),
                }
            }
        );
    }

    #[test]
    fn test_schedule_node() {
        let inspector = Inspector::new();
        let node = ScheduleNode::new(inspector.root().create_child("schedule"));
        let schedule = UpdateCheckSchedule::default();
        node.set(&schedule);

        assert_inspect_tree!(
            inspector,
            root: {
                schedule: {
                    schedule: format!("{:?}", schedule),
                }
            }
        );
    }

    #[test]
    fn test_protocol_state_node() {
        let inspector = Inspector::new();
        let node = ProtocolStateNode::new(inspector.root().create_child("protocol_state"));
        let protocol_state = ProtocolState::default();
        node.set(&protocol_state);

        assert_inspect_tree!(
            inspector,
            root: {
                protocol_state: {
                    protocol_state: format!("{:?}", protocol_state),
                }
            }
        );
    }

    #[test]
    fn test_last_results_node() {
        let inspector = Inspector::new();
        let mut node = LastResultsNode::new(inspector.root().create_child("last_results"));
        let result = Ok(update_check::Response {
            app_responses: vec![update_check::AppResponse {
                app_id: "some_id".to_string(),
                cohort: Cohort::default(),
                user_counting: UserCounting::ClientRegulatedByDate(None),
                result: update_check::Action::Updated,
            }],
        });
        node.add_result(SystemTime::UNIX_EPOCH, &result);
        node.add_result(SystemTime::UNIX_EPOCH + Duration::from_secs(100000), &result);

        assert_inspect_tree!(
            inspector,
            root: {
                last_results: {
                    "1970-01-01T00:00:00+00:00": {result: format!("{:#?}", result)},
                    "1970-01-02T03:46:40+00:00": {result: format!("{:#?}", result)},
                }
            }
        );

        for i in 0..10 {
            node.add_result(SystemTime::UNIX_EPOCH + Duration::from_secs(i * 1000000), &result);
        }
        assert_inspect_tree!(
            inspector,
            root: {
                last_results: {
                    "1970-01-01T00:00:00+00:00": {result: format!("{:#?}", result)},
                    "1970-01-12T13:46:40+00:00": {result: format!("{:#?}", result)},
                    "1970-01-24T03:33:20+00:00": {result: format!("{:#?}", result)},
                    "1970-02-04T17:20:00+00:00": {result: format!("{:#?}", result)},
                    "1970-02-16T07:06:40+00:00": {result: format!("{:#?}", result)},
                    "1970-02-27T20:53:20+00:00": {result: format!("{:#?}", result)},
                    "1970-03-11T10:40:00+00:00": {result: format!("{:#?}", result)},
                    "1970-03-23T00:26:40+00:00": {result: format!("{:#?}", result)},
                    "1970-04-03T14:13:20+00:00": {result: format!("{:#?}", result)},
                    "1970-04-15T04:00:00+00:00": {result: format!("{:#?}", result)},
                }
            }
        );
    }
}
