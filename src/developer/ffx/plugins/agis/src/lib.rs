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
// Vtc == Vulkan Traceable Component
struct Vtc {
    process_koid: u64,
    process_name: String,

    #[serde(skip)]
    agi_socket: fidl::Socket,
}

#[derive(PartialEq)]
struct VtcsResult {
    json: serde_json::Value,
    agi_sockets: Vec<fidl::Socket>,
}

impl std::fmt::Display for VtcsResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.json)
    }
}

impl Vtc {
    fn from_fidl(fidl_vtc: fidl_fuchsia_gpu_agis::Vtc) -> Result<Vtc, anyhow::Error> {
        Ok(Vtc {
            process_koid: fidl_vtc.process_koid.ok_or_else(|| {
                anyhow!(ffx_error!("\"agis\" service error. The \"process_koid\" is missing."))
            })?,
            process_name: fidl_vtc.process_name.ok_or_else(|| {
                anyhow!(ffx_error!("\"agis\" service error. The \"process_name\" is missing."))
            })?,
            agi_socket: fidl_vtc.agi_socket.ok_or_else(|| {
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
) -> Result<VtcsResult, anyhow::Error> {
    if op.process_name.is_empty() {
        return Err(anyhow!(ffx_error!("The \"register\" command requires a process name")));
    }
    let result = component_registry.register(op.id, op.process_koid, &op.process_name).await?;
    match result {
        Ok(_) => {
            // Create an arbitrary, valid socket to test as a return value.
            let (s, _) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
            let vtc =
                Vtc { process_koid: op.process_koid, process_name: op.process_name, agi_socket: s };
            let vtcs_result = VtcsResult { json: serde_json::to_value(&vtc)?, agi_sockets: vec![] };
            return Ok(vtcs_result);
        }
        Err(e) => {
            return Err(anyhow!(ffx_error!("The \"register\" command failed with error: {:?}", e)))
        }
    }
}

async fn component_registry_unregister(
    component_registry: ComponentRegistryProxy,
    op: UnregisterOp,
) -> Result<VtcsResult, anyhow::Error> {
    let result = component_registry.unregister(op.id).await?;
    match result {
        Ok(_) => {
            let vtcs_result = VtcsResult { json: serde_json::json!([{}]), agi_sockets: vec![] };
            return Ok(vtcs_result);
        }
        Err(e) => {
            return Err(anyhow!(ffx_error!(
                "The \"unregister\" command failed with error: {:?}",
                e
            )))
        }
    }
}

async fn observer_vtcs(observer: ObserverProxy) -> Result<VtcsResult, anyhow::Error> {
    let result = observer.vtcs().await?;
    match result {
        Ok(_fidl_vtcs) => {
            let mut vtcs = vec![];
            let mut agi_sockets = vec![];
            for fidl_vtc in _fidl_vtcs {
                let vtc = Vtc::from_fidl(fidl_vtc).unwrap();
                let (s, _) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
                vtcs.push(Vtc {
                    process_name: vtc.process_name,
                    process_koid: vtc.process_koid,
                    agi_socket: s,
                });
                agi_sockets.push(vtc.agi_socket);
            }
            let vtcs_result =
                VtcsResult { json: serde_json::to_value(&vtcs)?, agi_sockets: agi_sockets };
            return Ok(vtcs_result);
        }
        Err(e) => {
            return Err(anyhow!(ffx_error!("The \"vtcs\" command failed with error: {:?}", e)))
        }
    }
}

async fn agis_impl(
    component_registry: ComponentRegistryProxy,
    observer: ObserverProxy,
    cmd: AgisCommand,
) -> Result<VtcsResult, anyhow::Error> {
    match cmd.operation {
        Operation::Register(op) => component_registry_register(component_registry, op).await,
        Operation::Unregister(op) => component_registry_unregister(component_registry, op).await,
        Operation::Vtcs(_) => observer_vtcs(observer).await,
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, ffx_agis_args::VtcsOp, fidl_fuchsia_gpu_agis::ComponentRegistryRequest,
        fidl_fuchsia_gpu_agis::ObserverRequest,
    };

    const PROCESS_KOID: u64 = 999;
    const PROCESS_NAME: &str = "agis-vtcs-test";

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
                ObserverRequest::Vtcs { responder, .. } => {
                    let mut vtcs = vec![];
                    // Create an arbitrary, valid socket for use as the |agi_socket|.
                    let (s, _) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
                    vtcs.push(fidl_fuchsia_gpu_agis::Vtc {
                        process_koid: Some(PROCESS_KOID),
                        process_name: Some(PROCESS_NAME.to_string()),
                        agi_socket: Some(s),
                        unknown_data: None,
                        ..fidl_fuchsia_gpu_agis::Vtc::EMPTY
                    });
                    let mut result = Ok(vtcs);
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
    pub async fn vtcs() {
        let cmd = AgisCommand { operation: Operation::Vtcs(VtcsOp {}) };
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
