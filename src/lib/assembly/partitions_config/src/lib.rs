// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Library for reading and writing a description of the partitions for a
//! specific hardware.

mod partitions_config;

pub use partitions_config::{BootloaderPartition, Partition, PartitionsConfig, Slot};
