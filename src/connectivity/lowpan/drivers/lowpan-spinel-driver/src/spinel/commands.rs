// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

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
        response?;
        Ok(())
    }
}
