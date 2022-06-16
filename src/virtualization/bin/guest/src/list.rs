// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Context,
    anyhow::Error,
    fidl_fuchsia_virtualization::{GuestManagerProxy, GuestStatus, ManagerProxy},
};

pub async fn handle_list(manager: ManagerProxy) -> Result<String, Error> {
    let environments = manager.list().await.context("Could not fetch list of environments")?;

    if environments.len() == 0 {
        return Ok(String::from("no environments"));
    }
    let mut result_string = Vec::new();
    for env in environments {
        result_string.push(format!("env:{:<4}          {}", env.id, env.label));
        if env.instances.len() != 0 {
            for instance in env.instances {
                result_string.push(format!(" guest:{:<4}       {}", instance.cid, instance.label));
            }
        } else {
            result_string.push(String::from("no guest instances in environment"));
        }
    }
    Ok(result_string.join("\n"))
}

pub async fn handle_list_cfv2(
    managers: &Vec<(String, GuestManagerProxy)>,
) -> Result<String, Error> {
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
        fidl_fuchsia_virtualization::{
            EnvironmentInfo, GuestInfo, GuestManagerMarker, GuestManagerRequestStream,
            InstanceInfo, ManagerMarker, ManagerRequestStream,
        },
        fuchsia_async as fasync,
        futures::future::join,
        futures::{join, StreamExt},
        pretty_assertions::assert_eq,
    };

    #[fasync::run_until_stalled(test)]
    async fn list_valid_manager_no_environments_returns_ok() {
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        let mut test_env: [EnvironmentInfo; 0] = [];

        let server = create_mock_manager_server(stream, &mut test_env);

        let client = handle_list(proxy);
        let (_, client_res) = join(server, client).await;
        assert_eq!(client_res.unwrap(), "no environments");
    }

    #[fasync::run_until_stalled(test)]
    async fn list_valid_manager_no_instances_returns_ok() {
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        let mut test_env: [EnvironmentInfo; 1] =
            [EnvironmentInfo { id: 0, label: "testenv".to_string(), instances: Vec::new() }];

        let server = create_mock_manager_server(stream, &mut test_env);

        let client = handle_list(proxy);
        let (_, client_res) = join(server, client).await;
        let result_string: String =
            client_res.unwrap().chars().filter(|c| !c.is_whitespace()).collect();
        assert_eq!(result_string, "env:0testenvnoguestinstancesinenvironment");
    }

    #[fasync::run_until_stalled(test)]
    async fn list_valid_manager_env_instances_returns_ok() {
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        let test_instances = vec![
            InstanceInfo { cid: 0, label: "testinst".to_string() },
            InstanceInfo { cid: 1, label: "testinst2".to_string() },
        ];
        let mut test_env: [EnvironmentInfo; 1] =
            [EnvironmentInfo { id: 0, label: "testenv".to_string(), instances: test_instances }];

        let server = create_mock_manager_server(stream, &mut test_env);

        let client = handle_list(proxy);
        let (_, client_res) = join(server, client).await;
        // Remove all whitespace to prevent failures in future if we change the formatting
        let result_string: String =
            client_res.unwrap().chars().filter(|c| !c.is_whitespace()).collect();
        assert_eq!(result_string, "env:0testenvguest:0testinstguest:1testinst2");
    }

    async fn create_mock_manager_server(
        mut stream: ManagerRequestStream,
        test_env: &mut [EnvironmentInfo],
    ) -> Result<(), Error> {
        let responder = stream
            .next()
            .await
            .expect("Failed to read from stream")
            .expect("Failed to parse request")
            .into_list()
            .expect("Unexpected call to Manager Proxy");
        responder.send(&mut test_env.iter_mut()).expect("Failed to send request to proxy");

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn list_valid_managers_no_started_guests_returns_ok() {
        let (debian_proxy, debian_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();
        let (zircon_proxy, zircon_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();
        let (termina_proxy, termina_stream) =
            create_proxy_and_stream::<GuestManagerMarker>().unwrap();

        let zircon_server = create_mock_manager_server_cfv2(
            zircon_stream,
            GuestInfo { guest_status: GuestStatus::NotStarted },
        );
        let debian_server = create_mock_manager_server_cfv2(
            debian_stream,
            GuestInfo { guest_status: GuestStatus::NotStarted },
        );
        let termina_server = create_mock_manager_server_cfv2(
            termina_stream,
            GuestInfo { guest_status: GuestStatus::NotStarted },
        );

        let managers = vec![
            ("zircon".to_string(), zircon_proxy),
            ("debian".to_string(), debian_proxy),
            ("termina".to_string(), termina_proxy),
        ];
        let client = handle_list_cfv2(&managers);

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

        let zircon_server = create_mock_manager_server_cfv2(
            zircon_stream,
            GuestInfo { guest_status: GuestStatus::Started },
        );
        let debian_server = create_mock_manager_server_cfv2(
            debian_stream,
            GuestInfo { guest_status: GuestStatus::NotStarted },
        );
        let termina_server = create_mock_manager_server_cfv2(
            termina_stream,
            GuestInfo { guest_status: GuestStatus::Started },
        );

        let managers = vec![
            ("zircon".to_string(), zircon_proxy),
            ("debian".to_string(), debian_proxy),
            ("termina".to_string(), termina_proxy),
        ];
        let client = handle_list_cfv2(&managers);

        let (_, _, _, client_res) = join!(zircon_server, debian_server, termina_server, client);

        // Remove all whitespace to prevent failures in future if we change the formatting
        let result_string: String =
            client_res.unwrap().chars().filter(|c| !c.is_whitespace()).collect();
        assert_eq!(result_string, "guest:zirconguest:termina");
    }

    async fn create_mock_manager_server_cfv2(
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
