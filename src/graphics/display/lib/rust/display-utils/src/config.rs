// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    pixel_format::PixelFormat,
    types::{DisplayId, EventId, ImageId, LayerId},
};
use fidl_fuchsia_hardware_display as fdisplay;

/// LayerConfig is a variant type of the three distinct layer configuration types that are
/// supported by the display driver: Primary, Cursor, and Color.
// TODO(armansito): Complete the missing layer parameters.
#[derive(Clone, Debug)]
pub enum LayerConfig {
    /// A color layer contains a single color.
    Color {
        /// Pixel format of the color.
        pixel_format: PixelFormat,

        /// Bytes representing the color. This must conform to the layout implied by `pixel_format`.
        color_bytes: Vec<u8>,
    },

    /// A cursor layer can contain a cursor bitmap.
    Cursor,

    /// A primary layer is draws its pixels from a sysmem buffer backed image and supports various
    /// transofmations.
    Primary {
        /// The ID of the image that should be assigned to the primary layer. See the `image` mod
        /// in this crate to negotiate an image buffer with the display driver that can be used in
        /// this configuration.
        image_id: ImageId,

        /// Describes the dimensions, pixel format, and usage of the layer image.
        image_config: fdisplay::ImageConfig,

        /// When present, the display driver will not apply the configuration until the client
        /// signals this event.
        unblock_event: Option<EventId>,

        /// Event signaled by the display driver when a display configuration has been retired
        /// (i.e. it is no longer active) following the application of a new configuration.
        retirement_event: Option<EventId>,
    },
}

/// Represents an individual layer configuration.
#[derive(Clone, Debug)]
pub struct Layer {
    /// The ID of the layer. A layer ID can be obtained from a `Controller` instance by creating
    /// a layer.
    pub id: LayerId,

    /// Describes how the layer should be configured.
    pub config: LayerConfig,
}

/// Represents an individual display configuration.
#[derive(Clone, Debug)]
pub struct DisplayConfig {
    /// The ID of the display to configure.
    pub id: DisplayId,

    /// The list of layers in ascending z-order that should be assigned to the display.
    pub layers: Vec<Layer>,
}
