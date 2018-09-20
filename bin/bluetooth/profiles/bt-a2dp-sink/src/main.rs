// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[macro_use]
extern crate failure;

use failure::{Error, ResultExt};
use fidl_fuchsia_bluetooth_bredr::*;
use futures::{future, TryFutureExt, TryStreamExt};
use parking_lot::RwLock;

fn main() -> Result<(), Error> {
    let mut executor = fuchsia_async::Executor::new().context("error creating executor")?;

    let profile_svc = RwLock::new(
        fuchsia_app::client::connect_to_service::<ProfileMarker>()
            .context("Failed to connect to Bluetooth profile service")?,
    );

    let evt_stream = profile_svc.read().take_event_stream();

    let event_fut = evt_stream.try_for_each(move |evt| match evt {
        ProfileEvent::OnConnected {
            device_id,
            service_id: _,
            channel,
            protocol,
        } => {
            eprintln!("Connection from {}: {:?} {:?}!", device_id, channel, protocol);
            future::ready(Ok(()))
        }
        ProfileEvent::OnDisconnected {
            device_id,
            service_id,
        } => {
            eprintln!("Device {} disconnected from {}", device_id, service_id);
            future::ready(Ok(()))
        }
    });

    // Create and register the service
    let mut service_def = ServiceDefinition {
        service_class_uuids: vec![String::from("110B")], // Audio Sink UUID
        protocol_descriptors: vec![
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::L2Cap,
                params: vec![
                    DataElement {
                        type_: DataElementType::UnsignedInteger,
                        size: 2,
                        data: DataElementData::Integer(PSM_AVDTP),
                    },
                ],
            },
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::Avdtp,
                params: vec![
                    DataElement {
                        type_: DataElementType::UnsignedInteger,
                        size: 2,
                        data: DataElementData::Integer(0x0103), // Indicate v1.3
                    },
                ],
            },
        ],
        profile_descriptors: vec![
            ProfileDescriptor {
                profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
                major_version: 1,
                minor_version: 3,
            },
        ],
        additional_protocol_descriptors: None,
        information: vec![],
        additional_attributes: None,
    };

    let mut service_id = 0;

    let connect_fut = profile_svc
        .read()
        .add_service(&mut service_def, SecurityLevel::EncryptionOptional, false)
        .map_err(|_e| format_err!("can't send message"))
        .and_then(|(status, id)| {
            future::ready(match status.error {
                None => {
                    service_id = id;
                    Ok(())
                }
                Some(_e) => Err(format_err!("couldn't add service")),
            })
        });

    let fut = connect_fut.and_then(|_| event_fut.map_err(Into::into));
    executor.run_singlethreaded(fut).map_err(Into::into)
    // TODO(jamuraa): hook into media server to provide audio
    // TODO(jamuraa): accept connections and pass to AVDTP layer
}
