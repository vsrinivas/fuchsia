// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Error},
    fidl::endpoints::{create_endpoints, create_request_stream, ClientEnd},
    fidl_fuchsia_bluetooth_gatt::{
        self as gatt, LocalServiceDelegateOnReadValueResponder,
        LocalServiceDelegateRequest as ServiceDelegateReq,
        LocalServiceDelegateRequestStream as ServiceDelegateReqStream, LocalServiceMarker,
        Server_Proxy,
    },
    futures::{channel::mpsc, SinkExt, StreamExt},
    log::{info, warn},
};

use crate::host_dispatcher::HostDispatcher;

const GENERIC_ACCESS_SERVICE_UUID: &str = "00001800-0000-1000-8000-00805f9b34fb";
const GENERIC_ACCESS_DEVICE_NAME_UUID: &str = "00002A00-0000-1000-8000-00805f9b34fb";
const GENERIC_ACCESS_APPEARANCE_UUID: &str = "00002A01-0000-1000-8000-00805f9b34fb";
const GENERIC_ACCESS_DEVICE_NAME_ID: u64 = 0x2A00;
const GENERIC_ACCESS_APPEARANCE_ID: u64 = 0x2A01;

fn build_generic_access_service_info() -> gatt::ServiceInfo {
    // The spec says these characteristics should be readable, but optionally writeable. For
    // simplicity, we've disallowed them being peer-writeable. We enable access to these
    // characteristics with no security, as they are sent out freely during advertising anyway.
    let device_name_read_sec = Box::new(gatt::SecurityRequirements {
        encryption_required: false,
        authentication_required: false,
        authorization_required: false,
    });

    let appearance_read_sec = Box::new(gatt::SecurityRequirements {
        encryption_required: false,
        authentication_required: false,
        authorization_required: false,
    });

    let device_name_characteristic = gatt::Characteristic {
        id: GENERIC_ACCESS_DEVICE_NAME_ID,
        type_: GENERIC_ACCESS_DEVICE_NAME_UUID.to_string(),
        properties: gatt::PROPERTY_READ,
        permissions: Some(Box::new(gatt::AttributePermissions {
            read: Some(device_name_read_sec),
            write: None,
            update: None,
        })),
        descriptors: None,
    };

    let appearance_characteristic = gatt::Characteristic {
        id: GENERIC_ACCESS_APPEARANCE_ID,
        type_: GENERIC_ACCESS_APPEARANCE_UUID.to_string(),
        properties: gatt::PROPERTY_READ,
        permissions: Some(Box::new(gatt::AttributePermissions {
            read: Some(appearance_read_sec),
            write: None,
            update: None,
        })),
        descriptors: None,
    };

    gatt::ServiceInfo {
        // This value is ignored as this is a local-only service
        id: 0,
        // Secondary services are only rarely used and this is not one of those cases
        primary: true,
        type_: GENERIC_ACCESS_SERVICE_UUID.to_string(),
        characteristics: Some(vec![device_name_characteristic, appearance_characteristic]),
        includes: None,
    }
}

/// A GasProxy forwards peer Generic Accesss Service requests received by a BT host to the local GAS
/// task. A GasProxy will be spawned as a task by HostDispatcher whenever a new host is detected.
/// Passing the requests through proxies is preferable to the  task maintaining host state so that
/// we can limit host state to one place, HostDispatcher. This will simplify supporting multiple
/// Bluetooth hosts from within a single HostDispatcher in the future.
pub struct GasProxy {
    delegate_request_stream: ServiceDelegateReqStream,
    gas_task_channel: mpsc::Sender<ServiceDelegateReq>,
    // We have to hold on to these connections to the Hosts GATT server even though we never use them because
    // otherwise the host will shut down the connection to the Generic Access Server.
    _local_service_client: ClientEnd<LocalServiceMarker>,
    _gatt_server: Server_Proxy,
}

impl GasProxy {
    pub async fn new(
        gatt_server: Server_Proxy,
        gas_task_channel: mpsc::Sender<ServiceDelegateReq>,
    ) -> Result<GasProxy, Error> {
        let (delegate_client, delegate_request_stream) =
            create_request_stream::<gatt::LocalServiceDelegateMarker>()?;
        let (local_service_client, service_server) = create_endpoints::<LocalServiceMarker>()?;
        let mut service_info = build_generic_access_service_info();
        let status =
            gatt_server.publish_service(&mut service_info, delegate_client, service_server).await?;

        if let Some(error) = status.error {
            return Err(format_err!(
                "Failed to publish Generic Access Service to GATT server: {:?}",
                error
            ));
        }
        info!("Published Generic Access Service to local device database.");
        Ok(GasProxy {
            delegate_request_stream,
            gas_task_channel,
            _local_service_client: local_service_client,
            _gatt_server: gatt_server,
        })
    }

    pub async fn run(mut self) -> Result<(), Error> {
        while let Some(delegate_request) = self.delegate_request_stream.next().await {
            let delegate_inner_req = delegate_request?;
            if let Err(send_err) = self.gas_task_channel.send(delegate_inner_req).await {
                if send_err.is_disconnected() {
                    return Ok(());
                }
                return Err(send_err.into());
            }
        }
        Ok(())
    }
}

/// Struct holding the state needed to run the Generic Access Service task, which
/// serves requests to the Generic Access Service from other devices per the BT spec.
/// To avoid shared state it reads back into HostDispatcher to see the values of the
/// service characteristics (name/appearance). The stream of requests is abstracted
/// from being tied to a specific host - HostDispatcher is set up so that when any
/// new host is set up, it ties the sender end of that channel to an instance of
/// the GAS Proxy task, which proxies the requests from that specific host to the
/// sender end of the channel stored in this struct.
pub struct GenericAccessService {
    pub hd: HostDispatcher,
    pub generic_access_req_stream: mpsc::Receiver<ServiceDelegateReq>,
}

impl GenericAccessService {
    fn send_read_response(
        &self,
        responder: LocalServiceDelegateOnReadValueResponder,
        id: u64,
    ) -> Result<(), fidl::Error> {
        match id {
            GENERIC_ACCESS_DEVICE_NAME_ID => {
                let name = self.hd.get_name();
                responder.send(Some(name.as_bytes()), gatt::ErrorCode::NoError)
            }
            GENERIC_ACCESS_APPEARANCE_ID => {
                let appearance = self.hd.get_appearance() as u16;
                responder.send(Some(&appearance.to_le_bytes()), gatt::ErrorCode::NoError)
            }
            _ => responder.send(None, gatt::ErrorCode::NotPermitted),
        }
    }

    fn process_service_delegate_req(&self, request: ServiceDelegateReq) -> Result<(), Error> {
        match request {
            // Notifying peers is excluded from the characteristics in this service
            ServiceDelegateReq::OnCharacteristicConfiguration { .. } => Ok(()),
            ServiceDelegateReq::OnReadValue { responder, id, .. } => {
                Ok(self.send_read_response(responder, id)?)
            }
            // Writing to the the available GENERIC_ACCESS service characteristics
            // is optional according to the spec, and it was decided not to implement
            ServiceDelegateReq::OnWriteValue { responder, .. } => {
                Ok(responder.send(gatt::ErrorCode::NotPermitted)?)
            }
            ServiceDelegateReq::OnWriteWithoutResponse { .. } => Ok(()),
        }
    }

    pub async fn run(mut self) {
        while let Some(request) = self.generic_access_req_stream.next().await {
            self.process_service_delegate_req(request).unwrap_or_else(|e| {
                warn!("Error handling Generic Access Service Request: {:?}", e);
            });
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::store::stash::Stash,
        async_helpers::hanging_get::asynchronous as hanging_get,
        fidl::endpoints::{create_endpoints, create_proxy_and_stream},
        fidl_fuchsia_bluetooth::Appearance,
        fidl_fuchsia_bluetooth_gatt::{
            LocalServiceDelegateMarker, LocalServiceDelegateProxy,
            LocalServiceDelegateRequest as ServiceDelegateReq, LocalServiceMarker, Server_Marker,
        },
        fuchsia_async as fasync, fuchsia_inspect as inspect,
        futures::FutureExt,
        std::collections::HashMap,
    };

    const TEST_DEVICE_NAME: &str = "test-generic-access-service";
    const TEST_DEVICE_APPEARANCE: Appearance = Appearance::Computer;

    fn setup_generic_access_service() -> (LocalServiceDelegateProxy, HostDispatcher) {
        let (generic_access_delegate_client, delegate_request_stream) =
            create_proxy_and_stream::<LocalServiceDelegateMarker>().unwrap();
        let (local_service_client, _local_service_server) =
            create_endpoints::<LocalServiceMarker>().unwrap();
        let (gas_task_channel, generic_access_req_stream) = mpsc::channel::<ServiceDelegateReq>(0);
        let (gatt_server, _gatt_server_remote) =
            create_proxy_and_stream::<Server_Marker>().unwrap();
        let gas_proxy = GasProxy {
            delegate_request_stream,
            gas_task_channel: gas_task_channel.clone(),
            _local_service_client: local_service_client,
            _gatt_server: gatt_server,
        };
        fasync::Task::spawn(gas_proxy.run().map(|r| {
            r.unwrap_or_else(|err| {
                warn!("Error running Generic Access proxy in task: {:?}", err);
            })
        }))
        .detach();
        let stash = Stash::stub().expect("Create stash stub");
        let inspector = inspect::Inspector::new();
        let system_inspect = inspector.root().create_child("system");
        let watch_peers_broker = hanging_get::HangingGetBroker::new(
            HashMap::new(),
            |_, _| true,
            hanging_get::DEFAULT_CHANNEL_SIZE,
        );
        let watch_hosts_broker = hanging_get::HangingGetBroker::new(
            Vec::new(),
            |_, _| true,
            hanging_get::DEFAULT_CHANNEL_SIZE,
        );
        let dispatcher = HostDispatcher::new(
            TEST_DEVICE_NAME.to_string(),
            TEST_DEVICE_APPEARANCE,
            stash,
            system_inspect,
            gas_task_channel,
            watch_peers_broker.new_publisher(),
            watch_peers_broker.new_registrar(),
            watch_hosts_broker.new_publisher(),
            watch_hosts_broker.new_registrar(),
        );

        let service = GenericAccessService { hd: dispatcher.clone(), generic_access_req_stream };
        fasync::Task::spawn(service.run()).detach();
        (generic_access_delegate_client, dispatcher)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_change_name() {
        let (delegate_client, host_dispatcher) = setup_generic_access_service();
        let (expected_device_name, _err) =
            delegate_client.on_read_value(GENERIC_ACCESS_DEVICE_NAME_ID, 0).await.unwrap();
        assert_eq!(expected_device_name.unwrap(), TEST_DEVICE_NAME.as_bytes());
        host_dispatcher.set_name("test-generic-access-service-1".to_string()).await.unwrap_err();
        let (expected_device_name, _err) =
            delegate_client.on_read_value(GENERIC_ACCESS_DEVICE_NAME_ID, 0).await.unwrap();
        assert_eq!(
            expected_device_name.unwrap(),
            "test-generic-access-service-1".to_string().as_bytes()
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_appearance() {
        let (delegate_client, _host_dispatcher) = setup_generic_access_service();
        let (read_device_appearance, _err) =
            delegate_client.on_read_value(GENERIC_ACCESS_APPEARANCE_ID, 0).await.unwrap();
        assert_eq!(
            read_device_appearance.unwrap(),
            (TEST_DEVICE_APPEARANCE as u16).to_le_bytes().to_vec()
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_request() {
        let (delegate_client, _host_dispatcher) = setup_generic_access_service();
        let error_code = delegate_client
            .on_write_value(GENERIC_ACCESS_DEVICE_NAME_ID, 0, b"new-name")
            .await
            .unwrap();
        assert_eq!(error_code, gatt::ErrorCode::NotPermitted);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_gas_proxy() {
        let (generic_access_delegate_client, delegate_request_stream) =
            create_proxy_and_stream::<LocalServiceDelegateMarker>().unwrap();
        let (local_service_client, _local_service_server) =
            create_endpoints::<LocalServiceMarker>().unwrap();
        let (gas_task_channel, mut generic_access_req_stream) =
            mpsc::channel::<ServiceDelegateReq>(0);
        let (gatt_server, _gatt_server_remote) =
            create_proxy_and_stream::<Server_Marker>().unwrap();
        let gas_proxy = GasProxy {
            delegate_request_stream,
            gas_task_channel,
            _local_service_client: local_service_client,
            _gatt_server: gatt_server,
        };
        fasync::Task::spawn(gas_proxy.run().map(|r| {
            r.unwrap_or_else(|err| {
                warn!("Error running Generic Access proxy in task: {:?}", err);
            })
        }))
        .detach();
        let _ignored_fut =
            generic_access_delegate_client.on_read_value(GENERIC_ACCESS_APPEARANCE_ID, 0);
        let proxied_request = generic_access_req_stream.next().await.unwrap();
        if let ServiceDelegateReq::OnReadValue { id, .. } = proxied_request {
            assert_eq!(id, GENERIC_ACCESS_APPEARANCE_ID)
        } else {
            panic!("Unexpected request from GAS Proxy: {:?}", proxied_request);
        }
    }
}
