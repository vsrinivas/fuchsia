// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_hardware_display::Info, std::fmt};

/// Enhances the `fuchsia.hardware.display.Info` FIDL struct.
#[derive(Debug)]
pub struct DisplayInfo(pub Info);

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
        for (i, format) in self.0.pixel_format.iter().enumerate() {
            writeln!(f, "\t\t{}:\t{:#08x}", i, format)?;
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
                "\t\t{}:\t{:#08x} - {}x{}",
                i, config.pixel_format, config.width, config.height
            )?;
        }

        write!(f, "")
    }
}
