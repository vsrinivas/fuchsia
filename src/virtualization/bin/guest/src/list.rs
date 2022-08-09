// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Error,
    fidl_fuchsia_virtualization::{GuestManagerProxy, GuestStatus},
};

pub async fn handle_list(managers: &Vec<(String, GuestManagerProxy)>) -> Result<String, Error> {
    let mut result = String::new();
    for (guest_name, guest_manager) in managers {
        let guest_status = match guest_manager.get_guest_info().await {
            Ok(guest_info) => {
                if guest_info.guest_status == GuestStatus::Started {
                    "Started"
                } else {
                    "Stopped"
                }
            }
            Err(_) => "Unavailable",
        };
        result.push_str(format!(" guest:{:<4}\t{:<4}\n", guest_name, guest_status).as_str());
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
    async fn list_valid_managers_no_started_guests_two_guests_unavailable_returns_ok() {
        let (debian_proxy, debian_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();
        let (zircon_proxy, zircon_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();
        let (termina_proxy, termina_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();

        let zircon_server = create_mock_manager_server(zircon_stream, None);
        let debian_server = create_mock_manager_server(
            debian_stream,
            Some(GuestInfo { guest_status: GuestStatus::NotStarted }),
        );
        let termina_server = create_mock_manager_server(termina_stream, None);

        let managers = vec![
            ("zircon".to_string(), zircon_proxy),
            ("debian".to_string(), debian_proxy),
            ("termina".to_string(), termina_proxy),
        ];
        let client = handle_list(&managers);

        let (_, _, _, client_res) = join!(zircon_server, debian_server, termina_server, client);

        assert_eq!(
            client_res.unwrap(),
            " guest:zircon\tUnavailable\n guest:debian\tStopped\n guest:termina\tUnavailable\n"
        );
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
            Some(GuestInfo { guest_status: GuestStatus::Started }),
        );
        let debian_server = create_mock_manager_server(
            debian_stream,
            Some(GuestInfo { guest_status: GuestStatus::NotStarted }),
        );
        let termina_server = create_mock_manager_server(
            termina_stream,
            Some(GuestInfo { guest_status: GuestStatus::Started }),
        );

        let managers = vec![
            ("zircon".to_string(), zircon_proxy),
            ("debian".to_string(), debian_proxy),
            ("termina".to_string(), termina_proxy),
        ];
        let client = handle_list(&managers);

        let (_, _, _, client_res) = join!(zircon_server, debian_server, termina_server, client);

        assert_eq!(
            client_res.unwrap(),
            " guest:zircon\tStarted\n guest:debian\tStopped\n guest:termina\tStarted\n"
        );
    }

    async fn create_mock_manager_server(
        mut stream: GuestManagerRequestStream,
        guest_info: Option<GuestInfo>,
    ) -> Result<(), Error> {
        let responder = stream
            .next()
            .await
            .expect("Failed to read from stream")
            .expect("Failed to parse request")
            .into_get_guest_info()
            .expect("Unexpected call to Manager Proxy");
        if let Some(mut guest_info) = guest_info {
            responder.send(&mut guest_info).expect("Failed to send request to proxy");
        } else {
            drop(responder);
        };

        Ok(())
    }
}
