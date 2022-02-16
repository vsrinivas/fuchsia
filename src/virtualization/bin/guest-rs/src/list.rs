// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {anyhow::Context, anyhow::Error, fidl_fuchsia_virtualization::ManagerProxy};

pub async fn handle_list(manager: ManagerProxy) -> Result<String, Error> {
    let environments = manager.list().await.context("Could not fetch list of environments")?;

    if environments.len() == 0 {
        return Ok(String::from("no environments"));
    }
    let mut result_string = Vec::new();
    for env in environments {
        result_string.push(format!("env:{} {}", env.id, env.label));
        if env.instances.len() != 0 {
            for instance in env.instances {
                result_string.push(format!("guest:{} {}", instance.cid, instance.label));
            }
        } else {
            result_string.push(String::from("no guest instances in environment"));
        }
    }
    Ok(result_string.join("\n"))
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::{
            EnvironmentInfo, InstanceInfo, ManagerMarker, ManagerRequestStream,
        },
        fuchsia_async::{self as fasync, futures::StreamExt},
        futures::future::join,
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
        assert_eq!(client_res.unwrap(), "env:0 testenv\nno guest instances in environment");
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
        assert_eq!(client_res.unwrap(), "env:0 testenv\nguest:0 testinst\nguest:1 testinst2");
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
}
