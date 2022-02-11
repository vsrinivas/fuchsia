// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::Error,
    zwp_pointer_constraints_v1_server_protocol::{
        ZwpConfinedPointerV1, ZwpConfinedPointerV1Request, ZwpLockedPointerV1,
        ZwpLockedPointerV1Request, ZwpPointerConstraintsV1, ZwpPointerConstraintsV1Request,
    },
};

/// An implementation of the zwp_pointer_constraints_v1 global.
pub struct PointerConstraints;

impl RequestReceiver<ZwpPointerConstraintsV1> for PointerConstraints {
    fn receive(
        this: ObjectRef<Self>,
        request: ZwpPointerConstraintsV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZwpPointerConstraintsV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZwpPointerConstraintsV1Request::LockPointer { id, .. } => {
                id.implement(client, LockedPointer)?;
            }
            ZwpPointerConstraintsV1Request::ConfinePointer { id, .. } => {
                id.implement(client, ConfinedPointer)?;
            }
        }
        Ok(())
    }
}

struct LockedPointer;

impl RequestReceiver<ZwpLockedPointerV1> for LockedPointer {
    fn receive(
        this: ObjectRef<Self>,
        request: ZwpLockedPointerV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZwpLockedPointerV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZwpLockedPointerV1Request::SetCursorPositionHint { .. } => {}
            ZwpLockedPointerV1Request::SetRegion { .. } => {}
        }
        Ok(())
    }
}

struct ConfinedPointer;

impl RequestReceiver<ZwpConfinedPointerV1> for ConfinedPointer {
    fn receive(
        this: ObjectRef<Self>,
        request: ZwpConfinedPointerV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZwpConfinedPointerV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZwpConfinedPointerV1Request::SetRegion { .. } => {}
        }
        Ok(())
    }
}
