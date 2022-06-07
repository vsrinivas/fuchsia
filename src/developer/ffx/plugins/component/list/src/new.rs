// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ansi_term::Colour,
    anyhow::{Context, Result},
    component_hub::list::ListFilter,
    component_hub::new::list::{get_all_instances, Instance, InstanceState},
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon_status::Status,
    prettytable::{cell, format::consts::FORMAT_CLEAN, row, Table},
    serde::{Deserialize, Serialize},
};

#[derive(Deserialize, Serialize)]
pub struct SerializableInstance {
    // Moniker of the component. This is the full path in the component hierarchy.
    pub moniker: String,

    // URL of the component.
    pub url: String,

    // True if component is of appmgr/CMX type.
    // False if it is of the component_manager/CML type.
    pub is_cmx: bool,

    // CML components may not always be running.
    // Always true for CMX components.
    pub is_running: bool,
}

impl From<Instance> for SerializableInstance {
    fn from(i: Instance) -> Self {
        let is_running = i.state == InstanceState::Started;
        SerializableInstance {
            moniker: i.moniker.to_string(),
            url: i.url.unwrap_or_default(),
            is_cmx: i.is_cmx,
            is_running,
        }
    }
}

fn create_table(instances: Vec<Instance>) -> Table {
    let mut table = Table::new();
    table.set_format(*FORMAT_CLEAN);
    table.set_titles(row!("Type", "State", "Moniker", "URL"));

    for instance in instances {
        let component_type = if instance.is_cmx { "CMX" } else { "CML" };

        let state = if instance.state == InstanceState::Started {
            Colour::Green.paint("Running")
        } else {
            Colour::Red.paint("Stopped")
        };
        table.add_row(row!(
            component_type,
            state,
            instance.moniker.to_string(),
            instance.url.unwrap_or_default()
        ));
    }
    table
}

pub async fn list_impl(
    rcs_proxy: rc::RemoteControlProxy,
    mut writer: Writer,
    list_filter: Option<ListFilter>,
    verbose: bool,
) -> Result<()> {
    let (query_proxy, query_server) = fidl::endpoints::create_proxy::<fsys::RealmQueryMarker>()
        .context("creating hub root proxy")?;
    let (explorer_proxy, explorer_server) =
        fidl::endpoints::create_proxy::<fsys::RealmExplorerMarker>()
            .context("creating hub root proxy")?;
    rcs_proxy
        .root_realm_explorer(explorer_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening explorer")?;
    rcs_proxy
        .root_realm_query(query_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening query")?;

    let instances = get_all_instances(&explorer_proxy, &query_proxy, list_filter).await?;

    if writer.is_machine() {
        let instances: Vec<SerializableInstance> =
            instances.into_iter().map(SerializableInstance::from).collect();
        writer.machine(&instances)?;
    } else if verbose {
        let table = create_table(instances);
        table.print(&mut writer)?;
    } else {
        for instance in instances {
            writer.line(instance.moniker.to_string())?;
        }
    }
    Ok(())
}
