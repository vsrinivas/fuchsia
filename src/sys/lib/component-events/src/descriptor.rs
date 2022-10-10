// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::events::ExitStatus, fidl_fuchsia_sys2 as fsys, std::convert::TryFrom};

#[derive(Clone, Eq, PartialEq, PartialOrd, Ord, Debug)]
pub struct EventDescriptor {
    pub event_type: Option<fsys::EventType>,
    pub capability_name: Option<String>,
    pub target_moniker: Option<String>,
    pub exit_status: Option<ExitStatus>,
    pub event_is_ok: Option<bool>,
}

impl TryFrom<&fsys::Event> for EventDescriptor {
    type Error = anyhow::Error;

    fn try_from(event: &fsys::Event) -> Result<Self, Self::Error> {
        // Construct the EventDescriptor from the Event
        let event_type = event.header.as_ref().and_then(|header| header.event_type.clone());
        let target_moniker = event.header.as_ref().and_then(|header| header.moniker.clone());
        let capability_name = match &event.event_result {
            Some(fsys::EventResult::Payload(fsys::EventPayload::DirectoryReady(
                fsys::DirectoryReadyPayload { name, .. },
            ))) => name.clone(),
            Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityRequested(
                fsys::CapabilityRequestedPayload { name, .. },
            ))) => name.clone(),
            Some(fsys::EventResult::Error(fsys::EventError {
                error_payload:
                    Some(fsys::EventErrorPayload::DirectoryReady(fsys::DirectoryReadyError {
                        name,
                        ..
                    })),
                ..
            })) => name.clone(),
            Some(fsys::EventResult::Error(fsys::EventError {
                error_payload:
                    Some(fsys::EventErrorPayload::CapabilityRequested(fsys::CapabilityRequestedError {
                        name,
                        ..
                    })),
                ..
            })) => name.clone(),
            _ => None,
        };
        let exit_status = match &event.event_result {
            Some(fsys::EventResult::Payload(fsys::EventPayload::Stopped(
                fsys::StoppedPayload { status, .. },
            ))) => status.map(|val| val.into()),
            _ => None,
        };
        let event_is_ok = match &event.event_result {
            Some(fsys::EventResult::Payload(_)) => Some(true),
            Some(fsys::EventResult::Error(_)) => Some(false),
            _ => None,
        };

        Ok(EventDescriptor {
            event_type,
            target_moniker,
            capability_name,
            exit_status,
            event_is_ok,
        })
    }
}
