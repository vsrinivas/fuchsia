// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;

use anyhow::Error;
use core::fmt::Debug;
use spinel_pack::*;
use std::io;

/// Spinel No-Op Command.
///
/// Use with [`FrameHandler::send_request`].
#[derive(Debug, Copy, Clone)]
pub struct CmdNoop;

impl RequestDesc for CmdNoop {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::Noop.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        response?;
        Ok(())
    }
}

/// Spinel Software Reset Command.
///
/// Use with [`FrameHandler::send_request`].
#[derive(Debug, Copy, Clone)]
pub struct CmdReset;

impl RequestDesc for CmdReset {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::Reset.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        response?;
        Ok(())
    }
}

/// Spinel Clear Network Settings Command.
///
/// Use with [`FrameHandler::send_request`].
#[derive(Debug, Copy, Clone)]
pub struct CmdNetClear;

impl RequestDesc for CmdNetClear {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::NetClear.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        let frame = response?;
        match (frame.cmd, SpinelPropValueRef::try_unpack_from_slice(frame.payload)?) {
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::LastStatus, value }) => {
                match Status::try_owned_unpack_from_slice(value)? {
                    Status::Ok => Ok(()),
                    err => Err(err.into()),
                }
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::Net(PropNet::Saved), .. }) => {
                Ok(())
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop, value }) => Err(format_err!(
                "Unexpected response to \"net clear\": \"{:?} is {:?}\"",
                prop,
                value
            )),
            _ => Err(format_err!("Unexpected response to \"net clear\": {:?}", frame)),
        }
    }
}

/// Spinel Save Network Settings Command.
///
/// Use with [`FrameHandler::send_request`].
#[derive(Debug, Copy, Clone)]
pub struct CmdNetSave;

impl RequestDesc for CmdNetSave {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::NetSave.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        let frame = response?;
        match (frame.cmd, SpinelPropValueRef::try_unpack_from_slice(frame.payload)?) {
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::LastStatus, value }) => {
                match Status::try_owned_unpack_from_slice(value)? {
                    Status::Ok => Ok(()),
                    err => Err(err.into()),
                }
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::Net(PropNet::Saved), .. }) => {
                Ok(())
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop, value }) => Err(format_err!(
                "Unexpected response to \"net save\": \"{:?} is {:?}\"",
                prop,
                value
            )),
            _ => Err(format_err!("Unexpected response to \"net save\": {:?}", frame)),
        }
    }
}

/// Spinel Recall Network Settings Command.
///
/// Use with [`FrameHandler::send_request`].
#[derive(Debug, Copy, Clone)]
pub struct CmdNetRecall;

impl RequestDesc for CmdNetRecall {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::NetRecall.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        let frame = response?;
        match (frame.cmd, SpinelPropValueRef::try_unpack_from_slice(frame.payload)?) {
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::LastStatus, value }) => {
                match Status::try_owned_unpack_from_slice(value)? {
                    Status::Ok => Ok(()),
                    err => Err(err.into()),
                }
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::Net(PropNet::Saved), .. }) => {
                Ok(())
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop, value }) => Err(format_err!(
                "Unexpected response to \"net recall\": \"{:?} is {:?}\"",
                prop,
                value
            )),
            _ => Err(format_err!("Unexpected response to \"net recall\": {:?}", frame)),
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub struct CmdPropValueGet(pub Prop);

impl CmdPropValueGet {
    /// Combinator for interpreting the returned property value.
    pub fn returning<T: TryOwnedUnpack>(self) -> PropReturning<Self, T> {
        PropReturning::new(self)
    }
}

impl RequestDesc for CmdPropValueGet {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::PropValueGet.try_pack(buffer)?;
        self.0.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        let frame = response?;
        match (frame.cmd, SpinelPropValueRef::try_unpack_from_slice(frame.payload)?) {
            (Cmd::PropValueIs, SpinelPropValueRef { prop, .. }) if prop == self.0 => Ok(()),
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::LastStatus, value }) => {
                Err(<Status as TryUnpackAs<SpinelUint>>::try_unpack_as_from_slice(value)?.into())
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop, value }) => Err(format_err!(
                "Unexpected response to \"get {:?}\": \"{:?} is {:?}\"",
                self.0,
                prop,
                value
            )),
            _ => Err(format_err!("Unexpected response to \"get {:?}\": {:?}", self.0, frame)),
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub struct CmdPropValueSet<V: Debug>(pub Prop, pub V);

impl<V: Debug + TryPack + Send> CmdPropValueSet<V> {
    /// Combinator for interpreting the returned property value.
    #[allow(dead_code)]
    pub fn returning<T: TryOwnedUnpack>(self) -> PropReturning<Self, T> {
        PropReturning::new(self)
    }
}

impl<'v, V: TryUnpack<'v> + Debug> CmdPropValueSet<V> {
    /// Combinator that ensures that the value was set exactly as expected.
    ///
    /// This method doesn't do anything at the moment,
    /// but eventually it will verify that the 'VALUE_IS'
    /// response exactly matches what we were trying to set.
    pub fn verify(self) -> Self {
        self
    }
}

impl<V: TryPack + Debug + Send> RequestDesc for CmdPropValueSet<V> {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::PropValueSet.try_pack(buffer)?;
        self.0.try_pack(buffer)?;
        self.1.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        let frame = response?;
        match (frame.cmd, SpinelPropValueRef::try_unpack_from_slice(frame.payload)?) {
            (Cmd::PropValueIs, SpinelPropValueRef { prop, .. }) if prop == self.0 => Ok(()),
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::LastStatus, value }) => {
                Err(<Status as TryUnpackAs<SpinelUint>>::try_unpack_as_from_slice(value)?.into())
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop, value }) => Err(format_err!(
                "Unexpected response to \"set {:?}\": \"{:?} is {:?}\"",
                self.0,
                prop,
                value
            )),
            _ => Err(format_err!("Unexpected response to \"set {:?}\": {:?}", self.0, frame)),
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub struct CmdPropValueInsert<V: Debug>(pub Prop, pub V);

impl<V: TryPack + Debug + Send> RequestDesc for CmdPropValueInsert<V> {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::PropValueInsert.try_pack(buffer)?;
        self.0.try_pack(buffer)?;
        self.1.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        let frame = response?;
        match (frame.cmd, SpinelPropValueRef::try_unpack_from_slice(frame.payload)?) {
            (Cmd::PropValueIs, SpinelPropValueRef { prop, .. }) if prop == self.0 => Ok(()),
            (Cmd::PropValueInserted, SpinelPropValueRef { prop, .. }) if prop == self.0 => Ok(()),
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::LastStatus, value }) => {
                Err(<Status as TryUnpackAs<SpinelUint>>::try_unpack_as_from_slice(value)?.into())
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop, value }) => Err(format_err!(
                "Unexpected response to \"insert {:?}\": \"{:?} is {:?}\"",
                self.0,
                prop,
                value
            )),
            _ => Err(format_err!("Unexpected response to \"insert {:?}\": {:?}", self.0, frame)),
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub struct CmdPropValueRemove<V: Debug>(pub Prop, pub V);

impl<V: TryPack + Debug + Send> RequestDesc for CmdPropValueRemove<V> {
    type Result = ();

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        Cmd::PropValueRemove.try_pack(buffer)?;
        self.0.try_pack(buffer)?;
        self.1.try_pack(buffer)?;
        Ok(())
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        let frame = response?;
        match (frame.cmd, SpinelPropValueRef::try_unpack_from_slice(frame.payload)?) {
            (Cmd::PropValueIs, SpinelPropValueRef { prop, .. }) if prop == self.0 => Ok(()),
            (Cmd::PropValueRemoved, SpinelPropValueRef { prop, .. }) if prop == self.0 => Ok(()),
            (Cmd::PropValueIs, SpinelPropValueRef { prop: Prop::LastStatus, value }) => {
                Err(<Status as TryUnpackAs<SpinelUint>>::try_unpack_as_from_slice(value)?.into())
            }
            (Cmd::PropValueIs, SpinelPropValueRef { prop, value }) => Err(format_err!(
                "Unexpected response to \"remove {:?}\": \"{:?} is {:?}\"",
                self.0,
                prop,
                value
            )),
            _ => Err(format_err!("Unexpected response to \"remove {:?}\": {:?}", self.0, frame)),
        }
    }
}
