// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Tests for Bonding procedures
pub mod bonding;

/// Tests for fuchsia.bluetooth.sys.bootstrap protocol
pub mod bootstrap;

/// Tests for the fuchsia.bluetooth.control.Control protocol
pub mod control;

/// Tests for the Bluetooth Host driver behavior
pub mod host_driver;

/// Tests for the fuchsia.bluetooth.le.Central protocol
pub mod low_energy_central;

/// Tests for the fuchsia.bluetooth.le.Peripheral protocol
pub mod low_energy_peripheral;

/// Tests for the fuchsia.bluetooth.bredr.Profile protocol
pub mod profile;
