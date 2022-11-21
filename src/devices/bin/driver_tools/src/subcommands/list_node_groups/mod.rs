// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    crate::common::{node_property_key_to_string, node_property_value_to_string},
    anyhow::{Context, Result},
    args::ListNodeGroupsCommand,
    fidl_fuchsia_driver_development as fdd, fuchsia_driver_dev,
    std::io::Write,
};

pub async fn list_node_groups(
    cmd: ListNodeGroupsCommand,
    writer: &mut impl Write,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
) -> Result<()> {
    let node_groups = fuchsia_driver_dev::get_node_groups(&driver_development_proxy, cmd.name)
        .await
        .context("Failed to get node groups")?;

    if !cmd.verbose {
        for node_group in node_groups {
            let name = node_group.name.unwrap_or("N/A".to_string());
            let driver = node_group.driver.unwrap_or("None".to_string());
            writeln!(writer, "{:<20}: {}", name, driver)?;
        }
        return Ok(());
    }

    for node_group in node_groups {
        if let Some(name) = node_group.name {
            writeln!(writer, "{0: <10}: {1}", "Name", name)?;
        }

        if let Some(driver) = node_group.driver {
            writeln!(writer, "{0: <10}: {1}", "Driver", driver)?;
        } else {
            writeln!(writer, "{0: <10}: {1}", "Driver", "None")?;
        }

        if let Some(nodes) = node_group.nodes {
            writeln!(writer, "{0: <10}: {1}", "Nodes", nodes.len())?;

            for (i, node) in nodes.into_iter().enumerate() {
                let name = match &node_group.node_names {
                    Some(names) => format!("\"{}\"", names.get(i).unwrap()),
                    None => "None".to_string(),
                };

                if &node_group.primary_index == &Some(i as u32) {
                    writeln!(writer, "{0: <10}: {1} (Primary)", format!("Node {}", i), name)?;
                } else {
                    writeln!(writer, "{0: <10}: {1}", format!("Node {}", i), name)?;
                }

                let bind_rules_len = node.bind_rules.len();
                writeln!(writer, "  {0} {1}", bind_rules_len, "Bind Rules")?;

                for (j, bind_rule) in node.bind_rules.into_iter().enumerate() {
                    let key = node_property_key_to_string(&bind_rule.key);
                    let values = bind_rule
                        .values
                        .into_iter()
                        .map(|value| node_property_value_to_string(&value))
                        .collect::<Vec<_>>()
                        .join(", ");
                    writeln!(
                        writer,
                        "  [{0:>2}/{1:>2}] : {2:?} {3} {{ {4} }}",
                        j + 1,
                        bind_rules_len,
                        bind_rule.condition,
                        key,
                        values,
                    )?;
                }

                let bind_props_len = node.bind_properties.len();
                writeln!(writer, "  {0} {1}", bind_props_len, "Properties")?;

                for (j, bind_property) in node.bind_properties.into_iter().enumerate() {
                    let key = node_property_key_to_string(&bind_property.key.unwrap());
                    let value = node_property_value_to_string(&bind_property.value.unwrap());
                    writeln!(
                        writer,
                        "  [{0:>2}/{1:>2}] : Key {2:30} Value {3}",
                        j + 1,
                        bind_props_len,
                        key,
                        value,
                    )?;
                }
            }
        }

        writeln!(writer)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        argh::FromArgs,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_driver_framework as fdf, fuchsia_async as fasync,
        futures::{
            future::{Future, FutureExt},
            stream::StreamExt,
        },
    };

    /// Invokes `list_node_groups` with `cmd` and runs a mock driver development server that
    /// invokes `on_driver_development_request` whenever it receives a request.
    /// The output of `list_node_groups` that is normally written to its `writer` parameter
    /// is returned.
    async fn test_list_node_groups<F, Fut>(
        cmd: ListNodeGroupsCommand,
        on_driver_development_request: F,
    ) -> Result<String>
    where
        F: Fn(fdd::DriverDevelopmentRequest) -> Fut + Send + Sync + 'static,
        Fut: Future<Output = Result<()>> + Send + Sync,
    {
        let (driver_development_proxy, mut driver_development_requests) =
            fidl::endpoints::create_proxy_and_stream::<fdd::DriverDevelopmentMarker>()
                .context("Failed to create FIDL proxy")?;

        // Run the command and mock driver development server.
        let mut writer = Vec::new();
        let request_handler_task = fasync::Task::spawn(async move {
            while let Some(res) = driver_development_requests.next().await {
                let request = res.context("Failed to get next request")?;
                on_driver_development_request(request).await.context("Failed to handle request")?;
            }
            anyhow::bail!("Driver development request stream unexpectedly closed");
        });
        futures::select! {
            res = request_handler_task.fuse() => {
                res?;
                anyhow::bail!("Request handler task unexpectedly finished");
            }
            res = list_node_groups(cmd, &mut writer, driver_development_proxy).fuse() => res.context("List node groups command failed")?,
        }

        String::from_utf8(writer).context("Failed to convert list node groups output to a string")
    }

    async fn run_node_groups_iterator_server(
        mut node_groups: Vec<fdd::NodeGroupInfo>,
        iterator: ServerEnd<fdd::NodeGroupsIteratorMarker>,
    ) -> Result<()> {
        let mut iterator =
            iterator.into_stream().context("Failed to convert iterator into a stream")?;
        while let Some(res) = iterator.next().await {
            let request = res.context("Failed to get request")?;
            match request {
                fdd::NodeGroupsIteratorRequest::GetNext { responder } => {
                    responder
                        .send(
                            &mut node_groups
                                .drain(..)
                                .collect::<Vec<fdd::NodeGroupInfo>>()
                                .into_iter(),
                        )
                        .context("Failed to send node groups to responder")?;
                }
            }
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_verbose() {
        let cmd = ListNodeGroupsCommand::from_args(&["list-node-groups"], &["--verbose"]).unwrap();

        let output =
            test_list_node_groups(cmd, |request: fdd::DriverDevelopmentRequest| async move {
                match request {
                    fdd::DriverDevelopmentRequest::GetNodeGroups {
                        name_filter: _,
                        iterator,
                        control_handle: _,
                    } => run_node_groups_iterator_server(
                        vec![
                            fdd::NodeGroupInfo {
                                name: Some("test_group".to_string()),
                                nodes: Some(vec![fdf::NodeRepresentation {
                                    bind_rules: vec![fdf::BindRule {
                                        key: fdf::NodePropertyKey::StringValue(
                                            "rule_key".to_string(),
                                        ),
                                        condition: fdf::Condition::Accept,
                                        values: vec![fdf::NodePropertyValue::StringValue(
                                            "rule_val".to_string(),
                                        )],
                                    }],
                                    bind_properties: vec![fdf::NodeProperty {
                                        key: Some(fdf::NodePropertyKey::StringValue(
                                            "prop_key".to_string(),
                                        )),
                                        value: Some(fdf::NodePropertyValue::StringValue(
                                            "prop_val".to_string(),
                                        )),
                                        ..fdf::NodeProperty::EMPTY
                                    }],
                                }]),
                                ..fdd::NodeGroupInfo::EMPTY
                            },
                            fdd::NodeGroupInfo {
                                name: Some("test_group_with_driver".to_string()),
                                driver: Some("driver_url".to_string()),
                                primary_index: Some(1),
                                node_names: Some(vec![
                                    "name_one".to_string(),
                                    "name_two".to_string(),
                                ]),
                                nodes: Some(vec![
                                    fdf::NodeRepresentation {
                                        bind_rules: vec![fdf::BindRule {
                                            key: fdf::NodePropertyKey::StringValue(
                                                "rule_key".to_string(),
                                            ),
                                            condition: fdf::Condition::Accept,
                                            values: vec![
                                                fdf::NodePropertyValue::StringValue(
                                                    "rule_val".to_string(),
                                                ),
                                                fdf::NodePropertyValue::StringValue(
                                                    "rule_val_2".to_string(),
                                                ),
                                            ],
                                        }],
                                        bind_properties: vec![fdf::NodeProperty {
                                            key: Some(fdf::NodePropertyKey::StringValue(
                                                "prop_key_0".to_string(),
                                            )),
                                            value: Some(fdf::NodePropertyValue::StringValue(
                                                "prop_val_0".to_string(),
                                            )),
                                            ..fdf::NodeProperty::EMPTY
                                        }],
                                    },
                                    fdf::NodeRepresentation {
                                        bind_rules: vec![
                                            fdf::BindRule {
                                                key: fdf::NodePropertyKey::IntValue(0x0001),
                                                condition: fdf::Condition::Accept,
                                                values: vec![
                                                    fdf::NodePropertyValue::IntValue(0x42),
                                                    fdf::NodePropertyValue::IntValue(0x123),
                                                    fdf::NodePropertyValue::IntValue(0x234),
                                                ],
                                            },
                                            fdf::BindRule {
                                                key: fdf::NodePropertyKey::IntValue(0xdeadbeef),
                                                condition: fdf::Condition::Accept,
                                                values: vec![fdf::NodePropertyValue::IntValue(
                                                    0xbeef,
                                                )],
                                            },
                                        ],
                                        bind_properties: vec![
                                            fdf::NodeProperty {
                                                key: Some(fdf::NodePropertyKey::StringValue(
                                                    "prop_key_1".to_string(),
                                                )),
                                                value: Some(fdf::NodePropertyValue::EnumValue(
                                                    "prop_key_1.prop_val".to_string(),
                                                )),
                                                ..fdf::NodeProperty::EMPTY
                                            },
                                            fdf::NodeProperty {
                                                key: Some(fdf::NodePropertyKey::StringValue(
                                                    "prop_key_2".to_string(),
                                                )),
                                                value: Some(fdf::NodePropertyValue::IntValue(0x1)),
                                                ..fdf::NodeProperty::EMPTY
                                            },
                                            fdf::NodeProperty {
                                                key: Some(fdf::NodePropertyKey::StringValue(
                                                    "prop_key_3".to_string(),
                                                )),
                                                value: Some(fdf::NodePropertyValue::BoolValue(
                                                    true,
                                                )),
                                                ..fdf::NodeProperty::EMPTY
                                            },
                                        ],
                                    },
                                ]),
                                ..fdd::NodeGroupInfo::EMPTY
                            },
                        ],
                        iterator,
                    )
                    .await
                    .context("Failed to run driver info iterator server")?,
                    _ => {}
                }
                Ok(())
            })
            .await
            .unwrap();

        assert_eq!(
            output,
            r#"Name      : test_group
Driver    : None
Nodes     : 1
Node 0    : None
  1 Bind Rules
  [ 1/ 1] : Accept "rule_key" { "rule_val" }
  1 Properties
  [ 1/ 1] : Key "prop_key"                     Value "prop_val"

Name      : test_group_with_driver
Driver    : driver_url
Nodes     : 2
Node 0    : "name_one"
  1 Bind Rules
  [ 1/ 1] : Accept "rule_key" { "rule_val", "rule_val_2" }
  1 Properties
  [ 1/ 1] : Key "prop_key_0"                   Value "prop_val_0"
Node 1    : "name_two" (Primary)
  2 Bind Rules
  [ 1/ 2] : Accept fuchsia.BIND_PROTOCOL { 0x000042, 0x000123, 0x000234 }
  [ 2/ 2] : Accept 0xdeadbeef { 0x00beef }
  3 Properties
  [ 1/ 3] : Key "prop_key_1"                   Value Enum(prop_key_1.prop_val)
  [ 2/ 3] : Key "prop_key_2"                   Value 0x000001
  [ 3/ 3] : Key "prop_key_3"                   Value true

"#
        );
    }
}
