// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::device;
use anyhow::anyhow;
use std::str;

/// Provides a way to filter Devices to those specified by the user's preference.
pub struct Filter {
    bus: Option<u8>,
    dev: Option<u8>,
    func: Option<u8>,
}

impl Filter {
    pub fn matches(&self, device: &device::Device<'_>) -> bool {
        if let Some(bus) = &self.bus {
            if device.device.bus_id != *bus {
                return false;
            }
        }

        if let Some(dev) = &self.dev {
            if device.device.device_id != *dev {
                return false;
            }
        }

        if let Some(func) = &self.func {
            if device.device.function_id != *func {
                return false;
            }
        }

        true
    }
}

impl str::FromStr for Filter {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        // The expected format is BB:DD.F. The filter is permitted to omit
        // trailing sections as long as the sections provided are complete.
        //
        // ex: BB is fine, as is BB:DD, but BB:D is not.
        let vs: Vec<char> = s.chars().collect();
        if s.len() == 1 || s.len() == 4 || s.len() == 6 {
            return Err(anyhow!("filter is incomplete."));
        }

        if s.len() >= 3 && vs[2] != ':' {
            return Err(anyhow!(format!("expected ':', but found '{}' in position {}", vs[2], 2)));
        }

        if s.len() >= 6 && vs[5] != '.' {
            return Err(anyhow!(format!("expected '.', but found '{}' in position {}", vs[5], 5)));
        }

        if s.len() > 7 {
            return Err(anyhow!("filter is too long."));
        }

        let bus = if s.len() >= 2 { Some(u8::from_str_radix(&s[0..2], 16)?) } else { None };
        let dev = if s.len() >= 5 { Some(u8::from_str_radix(&s[3..5], 16)?) } else { None };
        let func = if s.len() == 7 { Some(u8::from_str_radix(&s[6..7], 16)?) } else { None };

        Ok(Filter { bus, dev, func })
    }
}
