// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_virtualization::{GuestManagerProxy, GuestStatus},
    fuchsia_zircon as zx, guest_cli_args as arguments,
    prettytable::{cell, format::consts::FORMAT_CLEAN, row, Table},
};

fn guest_status_to_string(status: GuestStatus) -> &'static str {
    match status {
        GuestStatus::NotStarted => "Not started",
        GuestStatus::Starting => "Starting",
        GuestStatus::Running => "Running",
        GuestStatus::Stopping => "Stopping",
        GuestStatus::Stopped => "Stopped",
        GuestStatus::VmmUnexpectedTermination => "VMM Unexpectedly Terminated",
    }
}

fn uptime_to_string(uptime: Option<zx::Duration>) -> String {
    match uptime {
        Some(uptime) => {
            if uptime < zx::Duration::from_nanos(0) {
                "Invalid negative uptime!".to_string()
            } else {
                let seconds = uptime.into_seconds() % 60;
                let minutes = uptime.into_minutes() % 60;
                let hours = uptime.into_hours();
                format!("{:0>2}:{:0>2}:{:0>2} HH:MM:SS", hours, minutes, seconds)
            }
        }
        None => "--:--:-- HH:MM:SS".to_string(),
    }
}

pub async fn get_detailed_information(
    guest_type: arguments::GuestType,
    manager: GuestManagerProxy,
) -> Result<String, Error> {
    let mut table = Table::new();
    table.set_format(*FORMAT_CLEAN);

    let guest_info = manager.get_info().await;
    if let Err(_) = guest_info {
        return Ok(format!("Failed to query guest information: {}", guest_type.to_string()));
    }
    let guest_info = guest_info.unwrap();
    let guest_status = guest_info.guest_status.expect("guest status should always be set");

    table.add_row(row!["Guest package:", guest_type.package_url()]);
    table.add_row(row!["Guest status:", guest_status_to_string(guest_status)]);
    table.add_row(row![
        "Guest uptime:",
        uptime_to_string(guest_info.uptime.map(|val| zx::Duration::from_nanos(val)))
    ]);

    if guest_status == GuestStatus::NotStarted {
        return Ok(table.to_string());
    }

    table.add_empty_row();

    if guest_status == GuestStatus::Stopped {
        table.add_row(row![
            "Stop reason:",
            guest_info
                .stop_error
                .map_or_else(|| "Clean shutdown".to_string(), |err| format!("{:?}", err))
        ]);
    } else {
        if guest_info.guest_descriptor.is_none() {
            return Ok(format!("{}WARNING: No config information found!", table.to_string()));
        }

        let config = guest_info.guest_descriptor.unwrap();

        table.add_row(row!["CPU count:", config.num_cpus.expect("all config fields must be set")]);
        table.add_row(row![
            "Guest memory:",
            format!(
                "{} GiB ({} bytes)",
                f64::trunc(
                    (config.guest_memory.expect("all config fields must be set") as f64
                        / (1 << 30) as f64)
                        * 100.0
                ) / 100.0,
                config.guest_memory.expect("all config fields must be set")
            )
        ]);

        table.add_empty_row();

        let mut active = Table::new();
        active.set_format(*FORMAT_CLEAN);
        let mut inactive = Table::new();
        inactive.set_format(*FORMAT_CLEAN);

        let mut add_to_table = |device: &str, is_active: Option<bool>| -> () {
            let is_active = is_active.expect("all config fields must be set");
            if is_active {
                active.add_row(row![device]);
            } else {
                inactive.add_row(row![device]);
            }
        };

        add_to_table("wayland", config.wayland);
        add_to_table("magma", config.magma);
        add_to_table("network", config.network);
        add_to_table("balloon", config.balloon);
        add_to_table("console", config.console);
        add_to_table("gpu", config.gpu);
        add_to_table("rng", config.rng);
        add_to_table("vsock", config.vsock);
        add_to_table("sound", config.sound);

        if active.len() == 0 {
            active.add_row(row!["None"]);
        }

        if inactive.len() == 0 {
            inactive.add_row(row!["None"]);
        }

        table.add_row(row!["Active devices:", active]);
        table.add_empty_row();
        table.add_row(row!["Inactive devices:", inactive]);
    }

    Ok(table.to_string())
}

pub async fn get_enviornment_summary(
    managers: Vec<(String, GuestManagerProxy)>,
) -> Result<String, Error> {
    let mut table = Table::new();
    table.set_titles(row!["Guest", "Status", "Uptime"]);

    for (name, manager) in managers {
        match manager.get_info().await {
            Ok(guest_info) => table.add_row(row![
                name,
                guest_status_to_string(
                    guest_info.guest_status.expect("guest status should always be set")
                ),
                uptime_to_string(guest_info.uptime.map(|val| zx::Duration::from_nanos(val)))
            ]),
            Err(_) => table.add_row(row![name, "Unavailable", "--:--:-- HH:MM:SS"]),
        };
    }

    Ok(table.to_string())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::{GuestDescriptor, GuestError, GuestInfo, GuestManagerMarker},
        fuchsia_async as fasync,
        futures::StreamExt,
    };

    fn serve_mock_manager(response: Option<GuestInfo>) -> GuestManagerProxy {
        let (proxy, mut stream) = create_proxy_and_stream::<GuestManagerMarker>()
            .expect("failed to create GuestManager proxy/stream");
        fasync::Task::local(async move {
            let responder = stream
                .next()
                .await
                .expect("mock manager expected a request")
                .unwrap()
                .into_get_info()
                .expect("unexpected call to mock manager");

            if let Some(guest_info) = response {
                responder.send(guest_info).expect("failed to send mock response");
            } else {
                drop(responder);
            }
        })
        .detach();

        proxy
    }

    #[fasync::run_until_stalled(test)]
    async fn negative_uptime() {
        // Note that a negative duration should never happen as we're measuring duration
        // monotonically from a single process.
        let duration = zx::Duration::from_seconds(-5);
        let actual = uptime_to_string(Some(duration));
        let expected = "Invalid negative uptime!";

        assert_eq!(actual, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn very_large_uptime() {
        let hours = zx::Duration::from_hours(123);
        let minutes = zx::Duration::from_minutes(45);
        let seconds = zx::Duration::from_seconds(54);
        let duration = hours + minutes + seconds;

        let actual = uptime_to_string(Some(duration));
        let expected = "123:45:54 HH:MM:SS";

        assert_eq!(actual, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn summarize_existing_managers() {
        let managers = vec![
            ("zircon".to_string(), serve_mock_manager(None)),
            ("termina".to_string(), serve_mock_manager(None)),
            (
                "debian".to_string(),
                serve_mock_manager(Some(GuestInfo {
                    guest_status: Some(GuestStatus::Running),
                    uptime: Some(zx::Duration::from_seconds(123).into_nanos()),
                    ..GuestInfo::EMPTY
                })),
            ),
        ];

        let actual = get_enviornment_summary(managers).await.unwrap();
        let expected = concat!(
            "+---------+-------------+-------------------+\n",
            "| Guest   | Status      | Uptime            |\n",
            "+=========+=============+===================+\n",
            "| zircon  | Unavailable | --:--:-- HH:MM:SS |\n",
            "+---------+-------------+-------------------+\n",
            "| termina | Unavailable | --:--:-- HH:MM:SS |\n",
            "+---------+-------------+-------------------+\n",
            "| debian  | Running     | 00:02:03 HH:MM:SS |\n",
            "+---------+-------------+-------------------+\n"
        );

        assert_eq!(actual, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn get_detailed_info_stopped_clean() {
        let manager = serve_mock_manager(Some(GuestInfo {
            guest_status: Some(GuestStatus::Stopped),
            uptime: Some(zx::Duration::from_seconds(5).into_nanos()),
            ..GuestInfo::EMPTY
        }));

        let actual =
            get_detailed_information(arguments::GuestType::Termina, manager).await.unwrap();
        let expected = concat!(
            " Guest package:  fuchsia-pkg://fuchsia.com/termina_guest#meta/termina_guest.cm \n",
            " Guest status:   Stopped \n",
            " Guest uptime:   00:00:05 HH:MM:SS \n",
            "                  \n",
            " Stop reason:    Clean shutdown \n",
        );

        assert_eq!(actual, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn get_detailed_info_stopped_guest_failure() {
        let manager = serve_mock_manager(Some(GuestInfo {
            guest_status: Some(GuestStatus::Stopped),
            uptime: Some(zx::Duration::from_seconds(65).into_nanos()),
            stop_error: Some(GuestError::InternalError),
            ..GuestInfo::EMPTY
        }));

        let actual = get_detailed_information(arguments::GuestType::Zircon, manager).await.unwrap();
        let expected = concat!(
            " Guest package:  fuchsia-pkg://fuchsia.com/zircon_guest#meta/zircon_guest.cm \n",
            " Guest status:   Stopped \n",
            " Guest uptime:   00:01:05 HH:MM:SS \n",
            "                  \n",
            " Stop reason:    InternalError \n",
        );

        assert_eq!(actual, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn get_detailed_info_running_guest() {
        let manager = serve_mock_manager(Some(GuestInfo {
            guest_status: Some(GuestStatus::Running),
            uptime: Some(zx::Duration::from_minutes(125).into_nanos()),
            guest_descriptor: Some(GuestDescriptor {
                num_cpus: Some(4),
                guest_memory: Some(1073741824),
                wayland: Some(false),
                magma: Some(false),
                network: Some(true),
                balloon: Some(true),
                console: Some(true),
                gpu: Some(false),
                rng: Some(true),
                vsock: Some(true),
                sound: Some(false),
                ..GuestDescriptor::EMPTY
            }),
            ..GuestInfo::EMPTY
        }));

        let actual = get_detailed_information(arguments::GuestType::Debian, manager).await.unwrap();
        let expected = concat!(
            " Guest package:     fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cm \n",
            " Guest status:      Running \n",
            " Guest uptime:      02:05:00 HH:MM:SS \n",
            "                     \n",
            " CPU count:         4 \n",
            " Guest memory:      1 GiB (1073741824 bytes) \n",
            "                     \n",
            " Active devices:     network  \n",
            "                     balloon  \n",
            "                     console  \n",
            "                     rng  \n",
            "                     vsock  \n",
            "                     \n",
            " Inactive devices:   wayland  \n",
            "                     magma  \n",
            "                     gpu  \n",
            "                     sound  \n"
        );

        assert_eq!(actual, expected);
    }
}
