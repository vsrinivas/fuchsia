// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    component_hub::{
        io::Directory,
        list::{Component, ListFilter},
    },
    ffx_component_list_args::ComponentListCommand,
    ffx_core::ffx_plugin,
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
    serde::{Deserialize, Serialize},
};

/// The number of times listing components from the hub should be retried before assuming failure.
const NUM_COMPONENT_LIST_ATTEMPTS: u64 = 3;

const SPACER: &str = "  ";
const WIDTH_CS_TREE: usize = 19;

#[derive(Deserialize, Serialize)]
pub struct ListComponent {
    // Name of the component. This is the full path in the component hierarchy.
    pub name: String,

    // True if component is of appmgr/CMX type.
    // False if it is of the component_manager/CML type.
    pub is_cmx: bool,

    // CML components may not always be running.
    // Always true for CMX components.
    pub is_running: bool,
}

impl ListComponent {
    fn new(leading: &str, own_name: &str, is_cmx: bool, is_running: bool) -> Self {
        let name = join_names(leading, own_name);
        Self { name, is_cmx, is_running }
    }
}

#[ffx_plugin()]
pub async fn list(
    rcs_proxy: rc::RemoteControlProxy,
    #[ffx(machine = Vec<ListComponent>)] writer: Writer,
    cmd: ComponentListCommand,
) -> Result<()> {
    list_impl(rcs_proxy, writer, cmd.only, cmd.verbose).await
}

// Attempt to get the component list `NUM_COMPONENT_LIST_ATTEMPTS` times. If all attempts fail, return the
// last error encountered.
//
// This fixes an issue (fxbug.dev/84805) where the component topology may be mutating while the
// hub is being traversed, resulting in failures.
pub async fn try_get_component_list(hub_dir: Directory, writer: &Writer) -> Result<Component> {
    let mut attempt_number = 1;
    loop {
        match Component::parse("/".to_string(), hub_dir.clone()?).await {
            Ok(component) => return Ok(component),
            Err(e) => {
                if attempt_number > NUM_COMPONENT_LIST_ATTEMPTS {
                    return Err(e);
                } else {
                    writer.error(format!("Retrying. Attempt #{} failed: {}", attempt_number, e))?;
                }
            }
        }
        attempt_number += 1;
    }
}

async fn list_impl(
    rcs_proxy: rc::RemoteControlProxy,
    writer: Writer,
    list_filter: Option<ListFilter>,
    verbose: bool,
) -> Result<()> {
    let (root, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("creating hub root proxy")?;
    rcs_proxy
        .open_hub(dir_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening hub")?;
    let hub_dir = Directory::from_proxy(root);

    let component = try_get_component_list(hub_dir, &writer).await?;

    let list_filter = list_filter.unwrap_or(ListFilter::None);

    if writer.is_machine() {
        writer.machine(&expand_component(component, &list_filter))?;
        Ok(())
    } else {
        print_component_tree(&component, &list_filter, verbose, 0, &writer)
    }
}

fn expand_component(component: Component, list_filter: &ListFilter) -> Vec<ListComponent> {
    let mut result = Vec::new();
    expand_component_rec("", component, list_filter, &mut result);
    result
}

fn expand_component_rec(
    leading: &str,
    component: Component,
    list_filter: &ListFilter,
    result: &mut Vec<ListComponent>,
) {
    let should_include_this = component.should_include(list_filter);
    let Component { name, is_cmx, is_running, children, .. } = component;

    if should_include_this {
        result.push(ListComponent::new(leading, &name, is_cmx, is_running));
    }

    let new_leading = join_names(leading, &name);
    for child in children {
        expand_component_rec(&new_leading, child, list_filter, result);
    }
}

pub fn print_component_tree(
    component: &Component,
    list_filter: &ListFilter,
    verbose: bool,
    indent: usize,
    writer: &Writer,
) -> Result<()> {
    let space = SPACER.repeat(indent);

    if component.should_include(&list_filter) {
        if verbose {
            let component_type = if component.is_cmx { "CMX" } else { "CML" };

            let state = if component.is_running { "Running" } else { "Stopped" };

            writer.line(format!(
                "{:<width_type$}{:<width_state$}{}{}",
                component_type,
                state,
                space,
                component.name,
                width_type = WIDTH_CS_TREE,
                width_state = WIDTH_CS_TREE
            ))?;
        } else {
            writer.line(format!("{}{}", space, component.name))?;
        }
    }

    for child in &component.children {
        print_component_tree(child, list_filter, verbose, indent + 1, writer)?;
    }

    Ok(())
}

fn join_names(first: &str, second: &str) -> String {
    if first.is_empty() {
        return second.to_string();
    }

    let mut result = String::from(first);

    if !result.ends_with('/') {
        result.push('/');
    }

    result.push_str(second);

    result
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_component_name_construction() {
        let leading = "/";
        let name = "foo.cmx";

        let list_component = ListComponent::new(leading, name, true, true);

        assert_eq!(list_component.name, "/foo.cmx");
    }

    fn component_for_test() -> Component {
        Component {
            name: "/".to_owned(),
            is_cmx: false,
            is_running: false,
            ancestors: vec![],
            children: vec![
                Component {
                    name: "appmgr".to_owned(),
                    is_cmx: false,
                    is_running: true,
                    ancestors: vec!["/".to_owned()],
                    children: vec![
                        Component {
                            name: "foo.cmx".to_owned(),
                            is_cmx: true,
                            is_running: true,
                            ancestors: vec!["/".to_owned(), "appmgr".to_owned()],
                            children: vec![],
                        },
                        Component {
                            name: "bar.cmx".to_owned(),
                            is_cmx: true,
                            is_running: true,
                            ancestors: vec!["/".to_owned(), "appmgr".to_owned()],
                            children: vec![],
                        },
                    ],
                },
                Component {
                    name: "sys".to_owned(),
                    is_cmx: false,
                    is_running: false,
                    ancestors: vec!["/".to_owned()],
                    children: vec![
                        Component {
                            name: "baz".to_owned(),
                            is_cmx: false,
                            is_running: true,
                            ancestors: vec!["/".to_owned(), "sys".to_owned()],
                            children: vec![],
                        },
                        Component {
                            name: "fuzz".to_owned(),
                            is_cmx: false,
                            is_running: false,
                            ancestors: vec!["/".to_owned(), "sys".to_owned()],
                            children: vec![Component {
                                name: "hello".to_owned(),
                                is_cmx: false,
                                is_running: false,
                                ancestors: vec![
                                    "/".to_owned(),
                                    "sys".to_owned(),
                                    "fuzz".to_owned(),
                                ],
                                children: vec![],
                            }],
                        },
                    ],
                },
            ],
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expand_component_no_filter() {
        let component = component_for_test();
        let filter = ListFilter::None;

        let result = expand_component(component, &filter);

        assert_eq!(result.len(), 8);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expand_component_running() {
        let component = component_for_test();
        let filter = ListFilter::Running;

        let result = expand_component(component, &filter);

        assert_eq!(result.len(), 4);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expand_component_stopped() {
        let component = component_for_test();
        let filter = ListFilter::Stopped;

        let result = expand_component(component, &filter);

        assert_eq!(result.len(), 4);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expand_component_cmx() {
        let component = component_for_test();
        let filter = ListFilter::CMX;

        let result = expand_component(component, &filter);

        assert_eq!(result.len(), 2);
        assert_eq!(result[0].name, "/appmgr/foo.cmx");
        assert_eq!(result[1].name, "/appmgr/bar.cmx");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expand_component_cml() {
        let component = component_for_test();
        let filter = ListFilter::CML;

        let result = expand_component(component, &filter);

        assert_eq!(result.len(), 6);
        assert_eq!(result[5].name, "/sys/fuzz/hello");
    }
}
