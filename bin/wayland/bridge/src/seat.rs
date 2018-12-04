// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_wayland_core as wl;
use wayland::{
    wl_seat, WlKeyboard, WlKeyboardRequest, WlPointer, WlPointerRequest, WlSeat, WlSeatEvent,
    WlSeatRequest, WlTouch, WlTouchRequest,
};

/// An implementation of the wl_seat global.
pub struct Seat;

impl Seat {
    /// Creates a new `Seat`.
    pub fn new() -> Self {
        Seat
    }

    pub fn post_seat_info(&self, this: wl::ObjectId, client: &mut wl::Client) -> Result<(), Error> {
        // TODO(tjdetwiler): Ideally we can source capabilities from scenic.
        // For now we'll report we can support all input types that scenic
        // supports.
        client.post(
            this,
            WlSeatEvent::Capabilities {
                capabilities: wl_seat::Capability::Pointer
                    | wl_seat::Capability::Keyboard
                    | wl_seat::Capability::Touch,
            },
        )?;
        client.post(
            this,
            WlSeatEvent::Name {
                name: "unknown".to_string(),
            },
        )?;
        Ok(())
    }
}

impl wl::RequestReceiver<WlSeat> for Seat {
    fn receive(
        this: wl::ObjectRef<Self>, request: WlSeatRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        match request {
            WlSeatRequest::Release => {
                client.delete_id(this.id())?;
            }
            WlSeatRequest::GetPointer { id } => {
                id.implement(client, InputDevice::Pointer)?;
            }
            WlSeatRequest::GetKeyboard { id } => {
                id.implement(client, InputDevice::Keyboard)?;
            }
            WlSeatRequest::GetTouch { id } => {
                id.implement(client, InputDevice::Touch)?;
            }
        }
        Ok(())
    }
}

enum InputDevice {
    Pointer,
    Keyboard,
    Touch,
}

impl wl::RequestReceiver<WlPointer> for InputDevice {
    fn receive(
        this: wl::ObjectRef<Self>, request: WlPointerRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        match request {
            WlPointerRequest::Release => {
                client.delete_id(this.id())?;
            }
            WlPointerRequest::SetCursor { .. } => {}
        }
        Ok(())
    }
}

impl wl::RequestReceiver<WlKeyboard> for InputDevice {
    fn receive(
        this: wl::ObjectRef<Self>, request: WlKeyboardRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        let WlKeyboardRequest::Release = request;
        client.delete_id(this.id())?;
        Ok(())
    }
}

impl wl::RequestReceiver<WlTouch> for InputDevice {
    fn receive(
        this: wl::ObjectRef<Self>, request: WlTouchRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        let WlTouchRequest::Release = request;
        client.delete_id(this.id())?;
        Ok(())
    }
}
