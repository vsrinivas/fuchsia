// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::ExitStatus, anyhow::format_err, fidl_fuchsia_sys2 as fsys, std::convert::TryFrom,
};

#[derive(Clone, Eq, PartialEq, PartialOrd, Ord, Debug)]
pub struct EventDescriptor {
    pub event_type: Option<fsys::EventType>,
    pub capability_id: Option<String>,
    pub target_moniker: Option<String>,
    pub exit_status: Option<ExitStatus>,
    pub event_is_ok: Option<bool>,
}

impl TryFrom<&fsys::Event> for EventDescriptor {
    type Error = anyhow::Error;

    fn try_from(event: &fsys::Event) -> Result<Self, Self::Error> {
        // Construct the EventDescriptor from the Event
        let event_type = Some(event.event_type.ok_or(format_err!("No event type"))?);
        let target_moniker =
            event.descriptor.as_ref().and_then(|descriptor| descriptor.moniker.clone());
        let capability_id = match &event.event_result {
            Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityReady(
                fsys::CapabilityReadyPayload { path, .. },
            ))) => path.clone(),
            Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityRequested(
                fsys::CapabilityRequestedPayload { path, .. },
            ))) => path.clone(),
            Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityRouted(
                fsys::CapabilityRoutedPayload { capability_id, .. },
            ))) => capability_id.clone(),
            Some(fsys::EventResult::Error(fsys::EventError {
                error_payload:
                    Some(fsys::EventErrorPayload::CapabilityReady(fsys::CapabilityReadyError {
                        path,
                        ..
                    })),
                ..
            })) => path.clone(),
            Some(fsys::EventResult::Error(fsys::EventError {
                error_payload:
                    Some(fsys::EventErrorPayload::CapabilityRequested(fsys::CapabilityRequestedError {
                        path,
                        ..
                    })),
                ..
            })) => path.clone(),
            Some(fsys::EventResult::Error(fsys::EventError {
                error_payload:
                    Some(fsys::EventErrorPayload::CapabilityRouted(fsys::CapabilityRoutedError {
                        capability_id,
                        ..
                    })),
                ..
            })) => capability_id.clone(),
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

        Ok(EventDescriptor { event_type, target_moniker, capability_id, exit_status, event_is_ok })
    }
}
