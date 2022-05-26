// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    errors::ffx_error,
    ffx_agis_args::{AgisCommand, Operation, RegisterOp, UnregisterOp},
    ffx_core::ffx_plugin,
    fidl_fuchsia_gpu_agis::ComponentRegistryProxy,
    fidl_fuchsia_gpu_agis::ObserverProxy,
    serde::Serialize,
};

#[derive(Serialize, Debug)]
struct Connection {
    process_koid: u64,
    process_name: String,

    #[serde(skip)]
    agi_socket: fidl::Socket,
}

#[derive(PartialEq)]
struct ConnectionsResult {
    json: serde_json::Value,
    agi_sockets: Vec<fidl::Socket>,
}

impl std::fmt::Display for ConnectionsResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.json)
    }
}

impl Connection {
    fn from_fidl(
        fidl_connection: fidl_fuchsia_gpu_agis::Connection,
    ) -> Result<Connection, anyhow::Error> {
        Ok(Connection {
            process_koid: fidl_connection.process_koid.ok_or_else(|| {
                anyhow!(ffx_error!("\"agis\" service error. The \"process_koid\" is missing."))
            })?,
            process_name: fidl_connection.process_name.ok_or_else(|| {
                anyhow!(ffx_error!("\"agis\" service error. The \"process_name\" is missing."))
            })?,
            agi_socket: fidl_connection.agi_socket.ok_or_else(|| {
                anyhow!(ffx_error!("\"agis\" service error. The \"agis_socket\" is missing."))
            })?,
        })
    }
}

#[ffx_plugin(
    ComponentRegistryProxy = "core/agis:expose:fuchsia.gpu.agis.ComponentRegistry",
    ObserverProxy = "core/agis:expose:fuchsia.gpu.agis.Observer"
)]
pub async fn agis(
    component_registry: ComponentRegistryProxy,
    observer: ObserverProxy,
    cmd: AgisCommand,
) -> Result<(), anyhow::Error> {
    println!("{}", agis_impl(component_registry, observer, cmd).await?);
    Ok(())
}

async fn component_registry_register(
    component_registry: ComponentRegistryProxy,
    op: RegisterOp,
) -> Result<ConnectionsResult, anyhow::Error> {
    if op.process_name.is_empty() {
        return Err(anyhow!(ffx_error!("The \"register\" command requires a process name")));
    }
    let result = component_registry.register(op.id, op.process_koid, &op.process_name).await?;
    match result {
        Ok(_) => {
            // Create an arbitrary, valid socket to test as a return value.
            let (s, _) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
            let connection = Connection {
                process_koid: op.process_koid,
                process_name: op.process_name,
                agi_socket: s,
            };
            let connections_result =
                ConnectionsResult { json: serde_json::to_value(&connection)?, agi_sockets: vec![] };
            return Ok(connections_result);
        }
        Err(e) => {
            return Err(anyhow!(ffx_error!("The \"register\" command failed with error: {:?}", e)))
        }
    }
}

async fn component_registry_unregister(
    component_registry: ComponentRegistryProxy,
    op: UnregisterOp,
) -> Result<ConnectionsResult, anyhow::Error> {
    let result = component_registry.unregister(op.id).await?;
    match result {
        Ok(_) => {
            let connections_result =
                ConnectionsResult { json: serde_json::json!([{}]), agi_sockets: vec![] };
            return Ok(connections_result);
        }
        Err(e) => {
            return Err(anyhow!(ffx_error!(
                "The \"unregister\" command failed with error: {:?}",
                e
            )))
        }
    }
}

async fn observer_connections(observer: ObserverProxy) -> Result<ConnectionsResult, anyhow::Error> {
    let result = observer.connections().await?;
    match result {
        Ok(_fidl_connections) => {
            let mut connections = vec![];
            let mut agi_sockets = vec![];
            for fidl_connection in _fidl_connections {
                let connection = Connection::from_fidl(fidl_connection).unwrap();
                let (s, _) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
                connections.push(Connection {
                    process_name: connection.process_name,
                    process_koid: connection.process_koid,
                    agi_socket: s,
                });
                agi_sockets.push(connection.agi_socket);
            }
            let connections_result = ConnectionsResult {
                json: serde_json::to_value(&connections)?,
                agi_sockets: agi_sockets,
            };
            return Ok(connections_result);
        }
        Err(e) => {
            return Err(anyhow!(ffx_error!(
                "The \"connections\" command failed with error: {:?}",
                e
            )))
        }
    }
}

async fn agis_impl(
    component_registry: ComponentRegistryProxy,
    observer: ObserverProxy,
    cmd: AgisCommand,
) -> Result<ConnectionsResult, anyhow::Error> {
    match cmd.operation {
        Operation::Register(op) => component_registry_register(component_registry, op).await,
        Operation::Unregister(op) => component_registry_unregister(component_registry, op).await,
        Operation::Connections(_) => observer_connections(observer).await,
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, ffx_agis_args::ConnectionsOp, fidl_fuchsia_gpu_agis::ComponentRegistryRequest,
        fidl_fuchsia_gpu_agis::ObserverRequest,
    };

    const PROCESS_KOID: u64 = 999;
    const PROCESS_NAME: &str = "agis-connections-test";

    fn fake_component_registry() -> ComponentRegistryProxy {
        let callback = move |req| {
            match req {
                ComponentRegistryRequest::Register { responder, .. } => {
                    // Create an arbitrary, valid socket to test as a return value.
                    let (s, _) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
                    responder.send(&mut Ok(s)).unwrap();
                }
                ComponentRegistryRequest::Unregister { responder, .. } => {
                    responder.send(&mut Ok(())).unwrap();
                }
            };
        };
        setup_fake_component_registry(callback)
    }

    fn fake_observer() -> ObserverProxy {
        let callback = move |req| {
            match req {
                ObserverRequest::Connections { responder, .. } => {
                    let mut connections = vec![];
                    // Create an arbitrary, valid socket for use as the |agi_socket|.
                    let (s, _) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
                    connections.push(fidl_fuchsia_gpu_agis::Connection {
                        process_koid: Some(PROCESS_KOID),
                        process_name: Some(PROCESS_NAME.to_string()),
                        agi_socket: Some(s),
                        unknown_data: None,
                        ..fidl_fuchsia_gpu_agis::Connection::EMPTY
                    });
                    let mut result = Ok(connections);
                    responder.send(&mut result).unwrap();
                }
            };
        };
        return setup_fake_observer(callback);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    pub async fn register() {
        let cmd = AgisCommand {
            operation: Operation::Register(RegisterOp {
                id: 0u64,
                process_koid: 0u64,
                process_name: "agis-register".to_string(),
            }),
        };

        let component_registry = fake_component_registry();
        let observer = fake_observer();
        let mut result = agis_impl(component_registry, observer, cmd).await;
        result.unwrap();

        let no_name_cmd = AgisCommand {
            operation: Operation::Register(RegisterOp {
                id: 0u64,
                process_koid: 0u64,
                process_name: "".to_string(),
            }),
        };
        let no_name_component_registry = fake_component_registry();
        let observer = fake_observer();
        result = agis_impl(no_name_component_registry, observer, no_name_cmd).await;
        assert!(result.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    pub async fn unregister() {
        let cmd = AgisCommand { operation: Operation::Unregister(UnregisterOp { id: 0u64 }) };
        let component_registry = fake_component_registry();
        let observer = fake_observer();
        let result = agis_impl(component_registry, observer, cmd).await;
        assert_eq!(result.unwrap().json, serde_json::json!([{}]));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    pub async fn connections() {
        let cmd = AgisCommand { operation: Operation::Connections(ConnectionsOp {}) };
        let component_registry = fake_component_registry();
        let observer = fake_observer();
        let result = agis_impl(component_registry, observer, cmd).await;
        let expected_output = serde_json::json!([{
            "process_koid": PROCESS_KOID,
            "process_name": PROCESS_NAME,
        }]);
        assert_eq!(result.unwrap().json, expected_output);
    }
}
