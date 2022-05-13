// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ansi_term::Colour,
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
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    prettytable::{cell, format::consts::FORMAT_CLEAN, row, Table},
    serde::{Deserialize, Serialize},
};

mod new;

/// The number of times listing components from the hub should be retried before assuming failure.
const NUM_COMPONENT_LIST_ATTEMPTS: u64 = 3;

#[derive(Deserialize, Serialize)]
pub struct ListComponent {
    // Name of the component. This is the full path in the component hierarchy.
    pub name: String,

    // URL of the component.
    pub url: String,

    // True if component is of appmgr/CMX type.
    // False if it is of the component_manager/CML type.
    pub is_cmx: bool,

    // CML components may not always be running.
    // Always true for CMX components.
    pub is_running: bool,
}

impl ListComponent {
    fn new(leading: &str, own_name: &str, url: String, is_cmx: bool, is_running: bool) -> Self {
        let name = join_names(leading, own_name);
        Self { name, url, is_cmx, is_running }
    }
}

#[ffx_plugin()]
pub async fn list(
    rcs_proxy: rc::RemoteControlProxy,
    #[ffx(machine = Vec<ListComponent>)] writer: Writer,
    cmd: ComponentListCommand,
) -> Result<()> {
    if let Ok(true) = ffx_config::get::<bool, _>("component.experimental.no_hub").await {
        crate::new::list_impl(rcs_proxy, writer, cmd.only, cmd.verbose).await
    } else {
        list_impl(rcs_proxy, writer, cmd.only, cmd.verbose).await
    }
}

// Attempt to get the component list `NUM_COMPONENT_LIST_ATTEMPTS` times. If all attempts fail, return the
// last error encountered.
//
// This fixes an issue (fxbug.dev/84805) where the component topology may be mutating while the
// hub is being traversed, resulting in failures.
pub async fn try_get_component_list(hub_dir: Directory, writer: &Writer) -> Result<Component> {
    let mut attempt_number = 1;
    loop {
        match Component::parse("/".to_string(), AbsoluteMoniker::root(), hub_dir.clone()?).await {
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
    mut writer: Writer,
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
    } else if verbose {
        let mut table = Table::new();
        table.set_format(*FORMAT_CLEAN);
        table.set_titles(row!("Type", "State", "Moniker", "URL"));
        add_component_rows_to_table(&component, &list_filter, &mut table)?;
        table.print(&mut writer)?;
        Ok(())
    } else {
        print_component_monikers(&component, &list_filter, &writer)?;
        Ok(())
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
    let Component { name, is_cmx, is_running, children, url, .. } = component;

    if should_include_this {
        result.push(ListComponent::new(leading, &name, url, is_cmx, is_running));
    }

    let new_leading = join_names(leading, &name);
    for child in children {
        expand_component_rec(&new_leading, child, list_filter, result);
    }
}

/// Recursively print the component monikers
///
/// # Arguments
///
/// * `component` - The component to print.
///                 Will recursively go through its children to print them too.
/// * `list_filter` - Filter to apply to the components.
/// * `writer` - The writer
pub fn print_component_monikers(
    component: &Component,
    list_filter: &ListFilter,
    writer: &Writer,
) -> Result<()> {
    if component.should_include(&list_filter) {
        writer.line(component.moniker.to_string())?;
    }

    for child in &component.children {
        print_component_monikers(child, list_filter, writer)?;
    }

    Ok(())
}

pub fn add_component_rows_to_table(
    component: &Component,
    list_filter: &ListFilter,
    table: &mut Table,
) -> Result<()> {
    if component.should_include(&list_filter) {
        let component_type = if component.is_cmx { "CMX" } else { "CML" };

        let state = if component.is_running {
            Colour::Green.paint("Running")
        } else {
            Colour::Red.paint("Stopped")
        };
        table.add_row(row!(component_type, state, component.moniker.to_string(), component.url));
    }
    for child in &component.children {
        add_component_rows_to_table(child, list_filter, table)?;
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
    use moniker::{AbsoluteMoniker, AbsoluteMonikerBase};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_component_name_construction() {
        let leading = "/";
        let name = "foo.cmx";

        let list_component = ListComponent::new(
            leading,
            name,
            "fuchsia-test://test.com/foo".to_string(),
            true,
            true,
        );

        assert_eq!(list_component.name, "/foo.cmx");
    }

    fn component_for_test() -> Component {
        Component {
            name: "/".to_owned(),
            moniker: AbsoluteMoniker::root(),
            is_cmx: false,
            url: "fuchsia-boot:///#meta/root.cm".to_owned(),
            is_running: false,
            children: vec![
                Component {
                    name: "appmgr".to_owned(),
                    moniker: AbsoluteMoniker::parse_str("/appmgr").unwrap(),
                    is_cmx: false,
                    url: "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_owned(),
                    is_running: true,
                    children: vec![
                        Component {
                            name: "foo.cmx".to_owned(),
                            moniker: AbsoluteMoniker::parse_str("/appmgr/foo.cmx").unwrap(),
                            is_cmx: true,
                            url: "fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx".to_owned(),
                            is_running: true,
                            children: vec![],
                        },
                        Component {
                            name: "bar.cmx".to_owned(),
                            moniker: AbsoluteMoniker::parse_str("/appmgr/bar.cmx").unwrap(),
                            is_cmx: true,
                            url: "fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx".to_owned(),
                            is_running: true,
                            children: vec![],
                        },
                    ],
                },
                Component {
                    name: "sys".to_owned(),
                    moniker: AbsoluteMoniker::parse_str("/sys").unwrap(),
                    is_cmx: false,
                    url: "fuchsia-pkg://fuchsia.com/sys#meta/sys.cm".to_owned(),
                    is_running: false,
                    children: vec![
                        Component {
                            name: "baz".to_owned(),
                            moniker: AbsoluteMoniker::parse_str("/sys/baz").unwrap(),
                            is_cmx: false,
                            url: "fuchsia-pkg://fuchsia.com/baz#meta/baz.cm".to_owned(),
                            is_running: true,
                            children: vec![],
                        },
                        Component {
                            name: "fuzz".to_owned(),
                            moniker: AbsoluteMoniker::parse_str("/sys/fuzz").unwrap(),
                            is_cmx: false,
                            url: "fuchsia-pkg://fuchsia.com/fuzz#meta/fuzz.cm".to_owned(),
                            is_running: false,
                            children: vec![Component {
                                name: "hello".to_owned(),
                                moniker: AbsoluteMoniker::parse_str("/sys/fuzz/hello").unwrap(),
                                is_cmx: false,
                                url: "fuchsia-pkg://fuchsia.com/hello#meta/hello.cm".to_owned(),
                                is_running: false,
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_component_rows_to_table() {
        let component = component_for_test();
        let filter = ListFilter::None;
        let mut table = Table::new();
        let result = add_component_rows_to_table(&component, &filter, &mut table);
        assert!(result.is_ok());
        assert_eq!(table.len(), 8);

        assert_eq!(
            table.get_row(0).unwrap(),
            &row!["CML", Colour::Red.paint("Stopped"), "/", "fuchsia-boot:///#meta/root.cm"]
        );
        assert_eq!(
            table.get_row(1).unwrap(),
            &row![
                "CML",
                Colour::Green.paint("Running"),
                "/appmgr",
                "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm"
            ]
        );
        assert_eq!(
            table.get_row(2).unwrap(),
            &row![
                "CMX",
                Colour::Green.paint("Running"),
                "/appmgr/foo.cmx",
                "fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx"
            ]
        );
        assert_eq!(
            table.get_row(3).unwrap(),
            &row![
                "CMX",
                Colour::Green.paint("Running"),
                "/appmgr/bar.cmx",
                "fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx"
            ]
        );
        assert_eq!(
            table.get_row(4).unwrap(),
            &row![
                "CML",
                Colour::Red.paint("Stopped"),
                "/sys",
                "fuchsia-pkg://fuchsia.com/sys#meta/sys.cm"
            ]
        );
        assert_eq!(
            table.get_row(5).unwrap(),
            &row![
                "CML",
                Colour::Green.paint("Running"),
                "/sys/baz",
                "fuchsia-pkg://fuchsia.com/baz#meta/baz.cm"
            ]
        );
        assert_eq!(
            table.get_row(6).unwrap(),
            &row![
                "CML",
                Colour::Red.paint("Stopped"),
                "/sys/fuzz",
                "fuchsia-pkg://fuchsia.com/fuzz#meta/fuzz.cm"
            ]
        );
        assert_eq!(
            table.get_row(7).unwrap(),
            &row![
                "CML",
                Colour::Red.paint("Stopped"),
                "/sys/fuzz/hello",
                "fuchsia-pkg://fuchsia.com/hello#meta/hello.cm"
            ]
        );
    }
}
