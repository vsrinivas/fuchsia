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
    process_id: u64,
    process_name: String,
    port: u16,
}

impl Connection {
    fn from_fidl(
        fidl_connection: fidl_fuchsia_gpu_agis::Connection,
    ) -> Result<Connection, anyhow::Error> {
        Ok(Connection {
            process_id: fidl_connection.process_id.ok_or_else(|| {
                anyhow!(ffx_error!("\"agis\" service error. The \"process_id\" is missing."))
            })?,
            process_name: fidl_connection.process_name.ok_or_else(|| {
                anyhow!(ffx_error!("\"agis\" service error. The \"process_name\" is missing."))
            })?,
            port: fidl_connection.port.ok_or_else(|| {
                anyhow!(ffx_error!("\"agis\" service error. The \"port\" is missing."))
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
) -> Result<serde_json::Value, anyhow::Error> {
    if op.process_name.is_empty() {
        return Err(anyhow!(ffx_error!("The \"register\" command requires a process name")));
    }
    let result = component_registry.register(op.process_id, &op.process_name).await?;
    match result {
        Ok(_) => {
            let connection =
                Connection { process_id: op.process_id, process_name: op.process_name, port: 0u16 };
            let json = serde_json::to_value(&connection)?;
            return Ok(json);
        }
        Err(e) => {
            return Err(anyhow!(ffx_error!("The \"register\" command failed with error: {:?}", e)))
        }
    }
}

async fn component_registry_unregister(
    component_registry: ComponentRegistryProxy,
    op: UnregisterOp,
) -> Result<serde_json::Value, anyhow::Error> {
    let result = component_registry.unregister(op.process_id).await?;
    match result {
        Ok(_) => return Ok(serde_json::json!([{}])),
        Err(e) => {
            return Err(anyhow!(ffx_error!(
                "The \"unregister\" command failed with error: {:?}",
                e
            )))
        }
    }
}

async fn observer_connections(observer: ObserverProxy) -> Result<serde_json::Value, anyhow::Error> {
    let result = observer.connections().await?;
    match result {
        Ok(_fidl_connections) => {
            let mut connections = vec![];
            for fidl_connection in _fidl_connections {
                connections.push(Connection::from_fidl(fidl_connection)?);
            }
            let json = serde_json::to_value(&connections)?;
            return Ok(json);
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
) -> Result<serde_json::Value, anyhow::Error> {
    match cmd.operation {
        Operation::Register(op) => component_registry_register(component_registry, op).await,
        Operation::Unregister(op) => component_registry_unregister(component_registry, op).await,
        Operation::Connections(_) => observer_connections(observer).await,
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, ffx_agis_args::ConnectionsOp, fidl::HandleBased,
        fidl_fuchsia_gpu_agis::ComponentRegistryRequest, fidl_fuchsia_gpu_agis::ObserverRequest,
    };

    const PORT: u16 = 100;
    const PROCESS_ID: u64 = 999;
    const PROCESS_NAME: &str = "agis-connections-test";

    fn fake_component_registry() -> ComponentRegistryProxy {
        let callback = move |req| {
            match req {
                ComponentRegistryRequest::Register { responder, .. } => {
                    // Create an arbitrary, valid handle to test as a return value.
                    let h = {
                        let (s, _) = fidl::Channel::create().unwrap();
                        s.into_handle()
                    };
                    responder.send(&mut Ok(Some(h))).unwrap();
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
                    connections.push(fidl_fuchsia_gpu_agis::Connection {
                        process_id: Some(PROCESS_ID),
                        process_name: Some(PROCESS_NAME.to_string()),
                        port: Some(PORT),
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
                process_id: 0u64,
                process_name: "agis-register".to_string(),
            }),
        };

        let component_registry = fake_component_registry();
        let observer = fake_observer();
        let mut result = agis_impl(component_registry, observer, cmd).await;
        result.unwrap();

        let no_name_cmd = AgisCommand {
            operation: Operation::Register(RegisterOp {
                process_id: 0u64,
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
        let cmd =
            AgisCommand { operation: Operation::Unregister(UnregisterOp { process_id: 0u64 }) };
        let component_registry = fake_component_registry();
        let observer = fake_observer();
        let result = agis_impl(component_registry, observer, cmd).await;
        assert_eq!(result.unwrap(), serde_json::json!([{}]));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    pub async fn connections() {
        let cmd = AgisCommand { operation: Operation::Connections(ConnectionsOp {}) };
        let component_registry = fake_component_registry();
        let observer = fake_observer();
        let result = agis_impl(component_registry, observer, cmd).await;
        let expected_output = serde_json::json!([{
            "process_id": PROCESS_ID,
            "process_name": PROCESS_NAME,
            "port": PORT,
        }]);
        assert_eq!(result.unwrap(), expected_output);
    }
}
