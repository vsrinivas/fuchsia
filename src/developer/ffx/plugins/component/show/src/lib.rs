// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ansi_term::Color,
    anyhow::Result,
    component_debug::show::{find_instances, Instance, Resolved},
    errors::ffx_bail,
    ffx_component::{connect_to_realm_explorer, connect_to_realm_query},
    ffx_component_show_args::ComponentShowCommand,
    ffx_core::ffx_plugin,
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol as rc,
    prettytable::{cell, format::consts::FORMAT_CLEAN, row, Table},
    std::io::Write,
};

#[ffx_plugin()]
pub async fn show(
    rcs_proxy: rc::RemoteControlProxy,
    #[ffx(machine = Vec<Instance>)] mut writer: Writer,
    cmd: ComponentShowCommand,
) -> Result<()> {
    let ComponentShowCommand { filter } = cmd;

    let query_proxy = connect_to_realm_query(&rcs_proxy).await?;
    let explorer_proxy = connect_to_realm_explorer(&rcs_proxy).await?;

    let instances = find_instances(filter.clone(), &explorer_proxy, &query_proxy).await?;

    if writer.is_machine() {
        writer.machine(&instances)?;
    } else {
        if instances.is_empty() {
            // TODO(fxbug.dev/104031): Clarify the exit code policy of this plugin.
            ffx_bail!("No matching components found for query \"{}\"", filter);
        }

        for instance in instances {
            let table = create_table(instance);
            table.print(&mut writer)?;
            writeln!(&mut writer, "")?;
        }
    }

    Ok(())
}

pub fn create_table(instance: Instance) -> Table {
    let mut table = Table::new();
    table.set_format(*FORMAT_CLEAN);

    add_basic_info_to_table(&mut table, &instance);
    add_resolved_info_to_table(&mut table, &instance);
    add_started_info_to_table(&mut table, &instance);

    table
}

fn add_basic_info_to_table(table: &mut Table, instance: &Instance) {
    table.add_row(row!(r->"Moniker:", instance.moniker));
    table.add_row(row!(r->"URL:", instance.url));

    if let Some(instance_id) = &instance.instance_id {
        table.add_row(row!(r->"Instance ID:", instance_id));
    } else {
        table.add_row(row!(r->"Instance ID:", "None"));
    }

    if instance.is_cmx {
        table.add_row(row!(r->"Type:", "CMX Component"));
    } else {
        table.add_row(row!(r->"Type:", "CML Component"));
    };
}

fn add_resolved_info_to_table(table: &mut Table, instance: &Instance) {
    if let Some(resolved) = &instance.resolved {
        table.add_row(row!(r->"Component State:", Color::Green.paint("Resolved")));
        let incoming_capabilities = resolved.incoming_capabilities.join("\n");
        let exposed_capabilities = resolved.exposed_capabilities.join("\n");
        table.add_row(row!(r->"Incoming Capabilities:", incoming_capabilities));
        table.add_row(row!(r->"Exposed Capabilities:", exposed_capabilities));

        if let Some(merkle_root) = &resolved.merkle_root {
            table.add_row(row!(r->"Merkle root:", merkle_root));
        }

        if let Some(config) = &resolved.config {
            if !config.is_empty() {
                let mut config_table = Table::new();
                let mut format = *FORMAT_CLEAN;
                format.padding(0, 0);
                config_table.set_format(format);

                for field in config {
                    config_table.add_row(row!(field.key, " -> ", field.value));
                }

                table.add_row(row!(r->"Configuration:", config_table));
            }
        }
    } else {
        table.add_row(row!(r->"Component State:", Color::Red.paint("Unresolved")));
    }
}

fn add_started_info_to_table(table: &mut Table, instance: &Instance) {
    if let Some(Resolved { started: Some(started), .. }) = &instance.resolved {
        table.add_row(row!(r->"Execution State:", Color::Green.paint("Running")));
        table.add_row(row!(r->"Start reason:", started.start_reason));

        if let Some(runtime) = &started.elf_runtime {
            if let Some(utc_estimate) = &runtime.process_start_time_utc_estimate {
                table.add_row(row!(r->"Running since:", utc_estimate));
            } else if let Some(ticks) = &runtime.process_start_time {
                table.add_row(row!(r->"Running for:", format!("{} ticks", ticks)));
            }

            table.add_row(row!(r->"Job ID:", runtime.job_id));

            if let Some(process_id) = &runtime.process_id {
                table.add_row(row!(r->"Process ID:", process_id));
            }
        }

        if let Some(outgoing_capabilities) = &started.outgoing_capabilities {
            let outgoing_capabilities = outgoing_capabilities.join("\n");
            table.add_row(row!(r->"Outgoing Capabilities:", outgoing_capabilities));
        }
    } else {
        table.add_row(row!(r->"Execution State:", Color::Red.paint("Stopped")));
    }
}
