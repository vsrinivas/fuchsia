// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Context,
    anyhow::Error,
    fidl_fuchsia_virtualization::{GuestManagerProxy, GuestStatus},
};

pub async fn handle_list(managers: &Vec<(String, GuestManagerProxy)>) -> Result<String, Error> {
    let mut result = String::new();
    for (guest_name, guest_manager) in managers {
        let guest_info =
            guest_manager.get_guest_info().await.context("Could not get guest info")?;
        if guest_info.guest_status == GuestStatus::Started {
            result.push_str(format!(" guest:{:<4}\n", guest_name).as_str());
        }
    }
    if result.is_empty() {
        result.push_str("no guests\n");
    }
    Ok(result)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::{GuestInfo, GuestManagerMarker, GuestManagerRequestStream},
        fuchsia_async as fasync,
        futures::{join, StreamExt},
        pretty_assertions::assert_eq,
    };

    #[fasync::run_until_stalled(test)]
    async fn list_valid_managers_no_started_guests_returns_ok() {
        let (debian_proxy, debian_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();
        let (zircon_proxy, zircon_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();
        let (termina_proxy, termina_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();

        let zircon_server = create_mock_manager_server(
            zircon_stream,
            GuestInfo { guest_status: GuestStatus::NotStarted },
        );
        let debian_server = create_mock_manager_server(
            debian_stream,
            GuestInfo { guest_status: GuestStatus::NotStarted },
        );
        let termina_server = create_mock_manager_server(
            termina_stream,
            GuestInfo { guest_status: GuestStatus::NotStarted },
        );

        let managers = vec![
            ("zircon".to_string(), zircon_proxy),
            ("debian".to_string(), debian_proxy),
            ("termina".to_string(), termina_proxy),
        ];
        let client = handle_list(&managers);

        let (_, _, _, client_res) = join!(zircon_server, debian_server, termina_server, client);

        let result_string = client_res.unwrap();
        assert_eq!(result_string, "no guests\n");
    }

    #[fasync::run_until_stalled(test)]
    async fn list_valid_managers_some_started_guests_returns_ok() {
        let (debian_proxy, debian_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();
        let (zircon_proxy, zircon_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();
        let (termina_proxy, termina_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();

        let zircon_server = create_mock_manager_server(
            zircon_stream,
            GuestInfo { guest_status: GuestStatus::Started },
        );
        let debian_server = create_mock_manager_server(
            debian_stream,
            GuestInfo { guest_status: GuestStatus::NotStarted },
        );
        let termina_server = create_mock_manager_server(
            termina_stream,
            GuestInfo { guest_status: GuestStatus::Started },
        );

        let managers = vec![
            ("zircon".to_string(), zircon_proxy),
            ("debian".to_string(), debian_proxy),
            ("termina".to_string(), termina_proxy),
        ];
        let client = handle_list(&managers);

        let (_, _, _, client_res) = join!(zircon_server, debian_server, termina_server, client);

        // Remove all whitespace to prevent failures in future if we change the formatting
        let result_string: String =
            client_res.unwrap().chars().filter(|c| !c.is_whitespace()).collect();
        assert_eq!(result_string, "guest:zirconguest:termina");
    }

    async fn create_mock_manager_server(
        mut stream: GuestManagerRequestStream,
        mut guest_info: GuestInfo,
    ) -> Result<(), Error> {
        let responder = stream
            .next()
            .await
            .expect("Failed to read from stream")
            .expect("Failed to parse request")
            .into_get_guest_info()
            .expect("Unexpected call to Manager Proxy");
        responder.send(&mut guest_info).expect("Failed to send request to proxy");

        Ok(())
    }
}
