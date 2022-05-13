// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ansi_term::Color,
    anyhow::{Context, Result},
    component_hub::new::show::{find_instances, Instance},
    ffx_writer::Writer,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon_status::Status,
};

pub async fn show_impl(
    rcs_proxy: rc::RemoteControlProxy,
    writer: Writer,
    query: &str,
) -> Result<()> {
    let (explorer_proxy, explorer_server) =
        fidl::endpoints::create_proxy::<fsys::RealmExplorerMarker>()
            .context("creating explorer proxy")?;
    let (query_proxy, query_server) = fidl::endpoints::create_proxy::<fsys::RealmQueryMarker>()
        .context("creating explorer proxy")?;
    rcs_proxy
        .root_realm_explorer(explorer_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening realm explorer")?;
    rcs_proxy
        .root_realm_query(query_server)
        .await?
        .map_err(|i| Status::ok(i).unwrap_err())
        .context("opening realm explorer")?;

    let instances = find_instances(query.to_string(), &explorer_proxy, &query_proxy).await?;

    if writer.is_machine() {
        writer.machine(&instances)?;
    } else {
        for instance in instances {
            let output = pretty_print_instance(instance)?;
            writer.line(output)?;
        }
    }

    Ok(())
}

macro_rules! pretty_print {
    ( $f: expr, $title: expr, $value: expr ) => {
        writeln!($f, "{:>22}: {}", $title, $value)
    };
}

macro_rules! pretty_print_list {
    ( $f: expr, $title: expr, $list: expr ) => {
        if !$list.is_empty() {
            writeln!($f, "{:>22}: {}", $title, &$list[0])?;
            for item in &$list[1..] {
                writeln!($f, "{:>22}  {}", " ", item)?;
            }
        }
    };
}

pub fn pretty_print_instance(instance: Instance) -> Result<String, std::fmt::Error> {
    let mut f = String::new();
    use std::fmt::Write;

    pretty_print!(f, "Moniker", instance.moniker)?;
    pretty_print!(f, "URL", instance.url)?;
    if let Some(component_id) = &instance.component_id {
        pretty_print!(f, "Component ID", component_id)?;
    }

    if instance.is_cmx {
        pretty_print!(f, "Type", "CMX Component")?;
    } else {
        pretty_print!(f, "Type", "CML Component")?;
    }

    if let Some(resolved) = &instance.resolved {
        pretty_print!(f, "Component State", Color::Green.paint("Resolved"))?;
        pretty_print_list!(f, "Incoming Capabilities", resolved.incoming_capabilities);
        pretty_print_list!(f, "Exposed Capabilities", resolved.exposed_capabilities);

        if let Some(merkle_root) = &resolved.merkle_root {
            pretty_print!(f, "Merkle root", merkle_root)?;
        }

        if let Some(config) = &resolved.config {
            if !config.is_empty() {
                let max_len = config.iter().map(|f| f.key.len()).max().unwrap();

                let first_field = &config[0];
                writeln!(
                    f,
                    "{:>22}: {:width$} -> {}",
                    "Configuration",
                    first_field.key,
                    first_field.value,
                    width = max_len
                )?;
                for field in &config[1..] {
                    writeln!(
                        f,
                        "{:>22}  {:width$} -> {}",
                        " ",
                        field.key,
                        field.value,
                        width = max_len
                    )?;
                }
            }
        }

        if let Some(started) = &resolved.started {
            pretty_print!(f, "Execution State", Color::Green.paint("Running"))?;
            pretty_print!(f, "Start reason", started.start_reason)?;

            if let Some(runtime) = &started.elf_runtime {
                if let Some(utc_estimate) = &runtime.process_start_time_utc_estimate {
                    pretty_print!(f, "Running since", utc_estimate)?;
                } else if let Some(ticks) = &runtime.process_start_time {
                    pretty_print!(f, "Running for", format!("{} ticks", ticks))?;
                }

                pretty_print!(f, "Job ID", runtime.job_id)?;

                if let Some(process_id) = &runtime.process_id {
                    pretty_print!(f, "Process ID", process_id)?;
                }
            }

            if let Some(outgoing_capabilities) = &started.outgoing_capabilities {
                pretty_print_list!(f, "Outgoing Capabilities", outgoing_capabilities);
            }
        } else {
            pretty_print!(f, "Execution State", Color::Red.paint("Stopped"))?;
        }
    } else {
        pretty_print!(f, "Component State", Color::Red.paint("Unresolved"))?;
    }

    Ok(f)
}
