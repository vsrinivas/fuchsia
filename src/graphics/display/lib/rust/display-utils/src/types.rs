// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{error::Result, pixel_format::PixelFormat};
use {
    fidl_fuchsia_hardware_display::Info,
    fuchsia_async::OnSignals,
    fuchsia_zircon::{self as zx, AsHandleRef},
    std::fmt,
};

/// Strongly typed wrapper around a display ID.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialOrd, PartialEq)]
pub struct DisplayId(pub u64);

/// Strongly typed wrapper around a display driver event ID.
#[derive(Clone, Copy, Debug)]
pub struct EventId(pub u64);

/// Strongly typed wrapper around a display layer ID.
#[derive(Clone, Copy, Debug)]
pub struct LayerId(pub u64);

/// Strongly typed wrapper around an image ID.
#[derive(Clone, Copy, Debug)]
pub struct ImageId(pub u64);

/// Strongly typed wrapper around a sysmem buffer collection ID.
#[derive(Clone, Copy, Debug)]
pub struct CollectionId(pub u64);

/// Enhances the `fuchsia.hardware.display.Info` FIDL struct.
#[derive(Clone, Debug)]
pub struct DisplayInfo(pub Info);

impl DisplayInfo {
    /// Returns the ID for this display.
    pub fn id(&self) -> DisplayId {
        DisplayId(self.0.id)
    }
}

/// Custom user-friendly format representation.
impl fmt::Display for DisplayInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "Display (id: {})", self.0.id)?;
        writeln!(f, "\tManufacturer Name: \"{}\"", self.0.manufacturer_name)?;
        writeln!(f, "\tMonitor Name: \"{}\"", self.0.monitor_name)?;
        writeln!(f, "\tMonitor Serial: \"{}\"", self.0.monitor_serial)?;
        writeln!(
            f,
            "\tPhysical Dimensions: {}mm x {}mm",
            self.0.horizontal_size_mm, self.0.vertical_size_mm
        )?;

        writeln!(f, "\tPixel Formats:")?;
        for (i, format) in self.0.pixel_format.iter().map(PixelFormat::from).enumerate() {
            writeln!(f, "\t\t{}:\t{}", i, format)?;
        }

        writeln!(f, "\tDisplay Modes:")?;
        for (i, mode) in self.0.modes.iter().enumerate() {
            writeln!(
                f,
                "\t\t{}:\t{:.2} Hz @ {}x{}",
                i,
                (mode.refresh_rate_e2 as f32) / 100.,
                mode.horizontal_resolution,
                mode.vertical_resolution
            )?;
        }
        writeln!(f, "\tCursor Configurations:")?;
        for (i, config) in self.0.cursor_configs.iter().enumerate() {
            writeln!(
                f,
                "\t\t{}:\t{} - {}x{}",
                i,
                PixelFormat::from(config.pixel_format),
                config.width,
                config.height
            )?;
        }

        write!(f, "")
    }
}

/// A zircon event that has been registered with the display driver.
pub struct Event {
    id: EventId,
    event: zx::Event,
}

impl Event {
    pub(crate) fn new(id: EventId, event: zx::Event) -> Event {
        Event { id, event }
    }

    /// Returns the ID for this event.
    pub fn id(&self) -> EventId {
        self.id
    }

    /// Returns a future that completes when the event has been signaled.
    pub async fn wait(&self) -> Result<()> {
        OnSignals::new(&self.event, zx::Signals::EVENT_SIGNALED).await?;
        self.event.as_handle_ref().signal(zx::Signals::EVENT_SIGNALED, zx::Signals::NONE)?;
        Ok(())
    }

    /// Signals the event.
    pub fn signal(&self) -> Result<()> {
        self.event.as_handle_ref().signal(zx::Signals::NONE, zx::Signals::EVENT_SIGNALED)?;
        Ok(())
    }
}
