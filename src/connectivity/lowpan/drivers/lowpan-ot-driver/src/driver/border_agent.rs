// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use super::*;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_net_mdns::*;
use fuchsia_component::client::connect_to_protocol;

const BORDER_AGENT_SERVICE_TYPE: &str = "_meshcop._udp.";

// Port 9 is the old-school discard port.
const BORDER_AGENT_SERVICE_PLACEHOLDER_PORT: u16 = 9;

// These flags are ultimately defined by table 8-5 of the Thread v1.1.1 specification.
// Additional flags originate from the source code found [here][1].
//
// [1]: https://github.com/openthread/ot-br-posix/blob/36db8891576a6ed571ad319afca734c5288c4cd9/src/border_agent/border_agent.cpp#L86
bitflags::bitflags! {
#[repr(C)]
#[derive(Default)]
pub struct BorderAgentState : u32 {
    const CONNECTION_MODE_PSKC = 1;
    const CONNECTION_MODE_PSKD = 2;
    const CONNECTION_MODE_VENDOR = 3;
    const CONNECTION_MODE_X509 = 4;
    const THREAD_IF_STATUS_INITIALIZED = (1<<3);
    const THREAD_IF_STATUS_ACTIVE = (2<<3);
    const HIGH_AVAILABILITY = (1<<5);
    const BBR_IS_ACTIVE = (1<<7);
    const BBR_IS_PRIMARY = (1<<8);
}
}

fn calc_meshcop_service_txt<OT>(
    ot_instance: &OT,
    vendor: &str,
    product: &str,
) -> Vec<(String, Vec<u8>)>
where
    OT: ot::InstanceInterface,
{
    let mut txt: Vec<(String, Vec<u8>)> = Vec::new();

    let mut border_agent_state =
        BorderAgentState::CONNECTION_MODE_PSKC | BorderAgentState::HIGH_AVAILABILITY;

    match ot_instance.get_device_role() {
        ot::DeviceRole::Disabled => {}
        ot::DeviceRole::Detached => {
            border_agent_state |= BorderAgentState::THREAD_IF_STATUS_INITIALIZED
        }
        _ => border_agent_state |= BorderAgentState::THREAD_IF_STATUS_ACTIVE,
    }

    // `rv` - Version of TXT record format.
    txt.push(("rv".to_string(), b"1".to_vec()));

    // `tv` - Version of Thread specification.
    txt.push(("tv".to_string(), b"1.2.0".to_vec()));

    // `sb` - State bitmap
    txt.push(("sb".to_string(), (border_agent_state.bits as u32).to_be_bytes().to_vec()));

    // `nn` - Network Name
    if ot_instance.is_commissioned() {
        match ot_instance.get_network_name().try_as_str() {
            Ok(nn) => txt.push(("nn".to_string(), nn.as_bytes().to_vec())),
            Err(err) => {
                warn!("Can't render network name: {:?}", err);
            }
        }

        // `xp` - Extended PAN-ID
        txt.push(("xp".to_string(), ot_instance.get_extended_pan_id().as_slice().to_vec()));
    }

    // `vn` - Vendor Name
    txt.push(("vn".to_string(), vendor.as_bytes().to_vec()));

    // `mn` - Model Name
    txt.push(("mn".to_string(), product.as_bytes().to_vec()));

    // `xa` - Extended Address
    txt.push(("xa".to_string(), ot_instance.get_extended_address().as_slice().to_vec()));

    if ot_instance.get_device_role().is_active() {
        let mut dataset = Default::default();

        match ot_instance.dataset_get_active(&mut dataset) {
            Ok(()) => {
                if let Some(at) = dataset.get_active_timestamp() {
                    // `at` - Active Operational Dataset Timestamp
                    txt.push(("at".to_string(), at.to_be_bytes().to_vec()));
                }
            }

            Err(err) => {
                warn!("Failed to get active dataset: {:?}", err);
            }
        }

        // `pt` - Partition ID
        txt.push(("pt".to_string(), ot_instance.get_partition_id().to_be_bytes().to_vec()));
    }

    txt
}

fn publish_border_agent_service(
    service_instance: String,
    txt: Vec<(String, Vec<u8>)>,
    port: u16,
) -> impl Future<Output = Result<(), anyhow::Error>> + 'static {
    let (client, server) =
        create_endpoints::<ServiceInstancePublicationResponder_Marker>().unwrap();

    let publisher = connect_to_protocol::<ServiceInstancePublisherMarker>().unwrap();

    let publish_init_future = publisher
        .publish_service_instance(
            BORDER_AGENT_SERVICE_TYPE,
            service_instance.as_str(),
            ServiceInstancePublicationOptions::EMPTY,
            client,
        )
        .map(|x| -> Result<(), anyhow::Error> {
            match x {
                Ok(Ok(x)) => Ok(x),
                Ok(Err(err)) => Err(anyhow::format_err!("{:?}", err)),
                Err(zx_err) => Err(zx_err.into()),
            }
        });

    // Render out the keys and values as ascii bytes.
    let txt = txt
        .iter()
        .map(|(key, value)| {
            let mut x = key.as_bytes().to_vec();
            x.push(b'=');
            x.extend(value.as_slice());
            x
        })
        .collect::<Vec<_>>();

    // Prepare our static response for all queries.
    let publication = Ok(ServiceInstancePublication {
        port: Some(port),
        text: Some(txt),
        ..ServiceInstancePublication::EMPTY
    });

    let publish_responder_future = server.into_stream().unwrap().map_err(Into::into).try_for_each(
        move |ServiceInstancePublicationResponder_Request::OnPublication {
                  publication_cause,
                  source_addresses,
                  responder,
                  ..
              }| {
            debug!(
                "meshcop: publish_border_agent_service: publication_cause: {:?}",
                publication_cause
            );
            debug!(
                "meshcop: publish_border_agent_service: source_addresses: {:?}",
                source_addresses
            );
            debug!(
                "meshcop: publish_border_agent_service: publication: {:?}",
                publication.as_ref().unwrap()
            );
            let mut publication = publication.clone();

            // Due to fxbug.dev/99755, the publication responder channel will close
            // if the publisher that created it is closed.
            // TODO(fxbug.dev/99755): Remove this line once fxbug.dev/99755 is fixed.
            let _ = publisher.clone();

            futures::future::ready(
                responder.send(&mut publication).context("Unable to call publication responder"),
            )
        },
    );

    futures::future::try_join(
        publish_init_future.inspect_err(|err| {
            warn!("meshcop: publish_border_agent_service: publish_init_future failed: {:?}", err);
        }),
        publish_responder_future.inspect_err(|err| {
            warn!(
                "meshcop: publish_border_agent_service: publish_responder_future failed: {:?}",
                err
            );
        }),
    )
    .map_ok(|_| ())
}

async fn get_product_info() -> Result<fidl_fuchsia_hwinfo::ProductInfo, anyhow::Error> {
    Ok(connect_to_protocol::<fidl_fuchsia_hwinfo::ProductMarker>()?.get_info().await?)
}

impl<OT: ot::InstanceInterface, NI, BI> OtDriver<OT, NI, BI> {
    pub async fn update_border_agent_service(&self) {
        let (vendor, product) = match get_product_info().await {
            Ok(info) => {
                let vendor = info.manufacturer.unwrap_or_else(|| "Unknown".to_string());
                let model = info.model.unwrap_or_else(|| "Fuchsia".to_string());
                (vendor, model)
            }
            Err(err) => {
                warn!("Unable to get product info: {:?}", err);
                ("Unknown".to_string(), "Fuchsia".to_string())
            }
        };

        // Add the last two bytes (in hex) of the extended address to the device name
        // to make the name more stable.
        let service_instance_name = {
            let driver_state = self.driver_state.lock();
            let ot_instance = &driver_state.ot_instance;
            format!(
                "{} ({})",
                product,
                hex::encode(&ot_instance.get_extended_address().as_slice()[6..])
            )
        };

        let (txt, port) = {
            let mut txt = self.border_agent_vendor_txt_entries.lock().await.clone();

            let driver_state = self.driver_state.lock();
            let ot_instance = &driver_state.ot_instance;
            txt.extend(calc_meshcop_service_txt(ot_instance, &vendor, &product));
            let port = if ot_instance.border_agent_get_state() != ot::BorderAgentState::Stopped {
                ot_instance.border_agent_get_udp_port()
            } else {
                // The following comment is from the original ot-br-posix implementation:
                // ---
                // When thread interface is not active, the border agent is not started,
                // thus it's not listening to any port and not handling requests. In such
                // situation, we use a placeholder port number for publishing the MeshCoP
                // service to advertise the status of the border router. One can learn
                // the thread interface status from `sb` entry so it doesn't have to send
                // requests to the placeholder port when border agent is not running.
                BORDER_AGENT_SERVICE_PLACEHOLDER_PORT
            };
            (txt, port)
        };

        let border_agent_current_txt_entries = self.border_agent_current_txt_entries.clone();
        let mut last_txt_entries = border_agent_current_txt_entries.lock().await;

        if txt == *last_txt_entries {
            debug!("meshcop: update_border_agent_service: No changes.");
        } else {
            debug!(
                "meshcop: update_border_agent_service: Updating meshcop dns-sd: port={} txt={:?}",
                port, txt
            );

            *last_txt_entries = txt.clone();

            let task = publish_border_agent_service(service_instance_name, txt, port);

            if let Err(err) = self
                .border_agent_service
                .lock()
                .replace(fasync::Task::spawn(task))
                .and_then(|x| x.cancel().now_or_never().flatten())
                .transpose()
            {
                warn!("meshcop: update_border_agent_service: Previous publication task ended with an error: {:?}", err);
            }
        }
    }
}
