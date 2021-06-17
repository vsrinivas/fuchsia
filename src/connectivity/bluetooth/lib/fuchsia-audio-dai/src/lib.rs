// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*! Fuchsia DAI Library

This library can be used to communicate with a Digital Audio Interface device as defined by
https://fuchsia.dev/fuchsia-src/concepts/drivers/driver_architectures/audio_drivers/audio_dai
using the driver node directly.

Components using the library will need to have access to the dai class of devices in their component
 manifest.

*/

/// Driver library for querying and configuring a known DAI device.
pub mod driver;
pub use driver::DigitalAudioInterface;

/// Discovery library to enumerate devices available to a component.
pub mod discover;
pub use discover::find_devices;

/// Fuchsia Audio device integration.
pub mod audio;
pub use audio::DaiAudioDevice;
