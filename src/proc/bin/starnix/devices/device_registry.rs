// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

use crate::devices::*;
use crate::types::uapi::*;

#[derive(Hash, PartialEq, Eq, Debug, Copy, Clone)]
pub enum AnonymousNodeDeviceName {
    Pipe,
}

fn make_device_id(major: u16, minor: u32) -> dev_t {
    let major = major as dev_t;
    let minor = minor as dev_t;
    (minor & 0xff) | ((major & 0xfff) << 8) | ((minor >> 8) << 20)
}

pub struct DeviceRegistry {
    next_anonymous_device_minor_number: AtomicU32,
    anonymous_node_devices: Mutex<HashMap<AnonymousNodeDeviceName, DeviceHandle>>,
}

impl DeviceRegistry {
    pub fn new() -> DeviceRegistry {
        DeviceRegistry {
            next_anonymous_device_minor_number: AtomicU32::new(1),
            anonymous_node_devices: Mutex::new(HashMap::new()),
        }
    }

    pub fn get_anonymous_node_device(&self, name: AnonymousNodeDeviceName) -> DeviceHandle {
        let mut devices = self.anonymous_node_devices.lock();
        Arc::clone(devices.entry(name).or_insert_with(|| {
            AnonymousNodeDevice::new(self.allocate_anonymous_device_minor_number())
        }))
    }

    fn allocate_anonymous_device_minor_number(&self) -> dev_t {
        let minor = self.next_anonymous_device_minor_number.fetch_add(1, Ordering::Relaxed);
        make_device_id(0, minor)
    }
}
