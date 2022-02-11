// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::client::EventQueue,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::Error,
    fuchsia_wayland_core as wl,
    zwp_relative_pointer_v1_server_protocol::{
        ZwpRelativePointerManagerV1, ZwpRelativePointerManagerV1Request, ZwpRelativePointerV1,
        ZwpRelativePointerV1Event, ZwpRelativePointerV1Request,
    },
};

/// An implementation of the zwp_relative_pointer_v1 global.
pub struct RelativePointerManager;

impl RequestReceiver<ZwpRelativePointerManagerV1> for RelativePointerManager {
    fn receive(
        this: ObjectRef<Self>,
        request: ZwpRelativePointerManagerV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZwpRelativePointerManagerV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZwpRelativePointerManagerV1Request::GetRelativePointer { id, pointer: _ } => {
                let relative_pointer = id.implement(client, RelativePointer)?;
                // Assert here because if we successfully implemented the
                // interface the given id is valid. Any failure here indicates
                // a coherence issue between the client object map and the set
                // of bound pointers.
                assert!(client.input_dispatcher.relative_pointers.add(relative_pointer));
            }
        }
        Ok(())
    }
}

pub struct RelativePointer;

impl RelativePointer {
    pub fn post_relative_motion(
        this: ObjectRef<Self>,
        event_queue: &EventQueue,
        time_in_us: u64,
        dx: wl::Fixed,
        dy: wl::Fixed,
    ) -> Result<(), Error> {
        event_queue.post(
            this.id(),
            ZwpRelativePointerV1Event::RelativeMotion {
                utime_hi: (time_in_us >> 32) as u32,
                utime_lo: (time_in_us & 0xffffffff) as u32,
                dx,
                dy,
                dx_unaccel: dx,
                dy_unaccel: dy,
            },
        )
    }
}

impl RequestReceiver<ZwpRelativePointerV1> for RelativePointer {
    fn receive(
        this: ObjectRef<Self>,
        request: ZwpRelativePointerV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZwpRelativePointerV1Request::Destroy => {
                client.input_dispatcher.relative_pointers.remove(this);
                client.delete_id(this.id())?;
            }
        }
        Ok(())
    }
}
