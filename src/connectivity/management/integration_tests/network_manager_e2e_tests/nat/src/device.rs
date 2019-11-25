// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Device type and status information.

use structopt::StructOpt;

/// Type of device.
#[derive(Clone, Copy, Debug, PartialEq, StructOpt)]
#[structopt(rename_all = "snake")]
#[repr(u8)]
pub enum Type {
    Lan,
    Wan,
    Router,
}

static TYPES: [(&str, Type); 3] = [
    ("nmh::nat::lan", Type::Lan),
    ("nmh::nat::wan", Type::Wan),
    ("nmh::nat::router", Type::Router),
];

impl From<String> for Type {
    fn from(s: String) -> Type {
        *TYPES
            .iter()
            .find_map(|(n, v)| if **n == s { Some(v) } else { None })
            .expect(format!("unexpected device: {}", s).as_str())
    }
}

impl From<Type> for &'static str {
    fn from(t: Type) -> &'static str {
        TYPES[t as usize].0
    }
}

impl std::fmt::Display for Type {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", Into::<&'static str>::into(*self))
    }
}

impl Type {
    pub fn others(&self) -> [Type; 2] {
        match self {
            Type::Lan => [Type::Wan, Type::Router],
            Type::Wan => [Type::Lan, Type::Router],
            Type::Router => [Type::Lan, Type::Wan],
        }
    }
}

/// Status of the device. Represented as a bitset of STATUS_* values.
#[derive(Clone, Copy, PartialEq)]
pub struct Status(u8);

pub static STATUS_UNKNOWN: Status = Status(0b0000);
pub static STATUS_ATTACHED: Status = Status(0b0001);
pub static STATUS_READY: Status = Status(0b0010);
pub static STATUS_FINISHED: Status = Status(0b0100);
pub static STATUS_ERROR: Status = Status(0b1000);

impl From<i32> for Status {
    fn from(i: i32) -> Status {
        if i > 0b0000_1000 {
            panic!("invalid status: {:?}", i)
        }
        Status(i as u8)
    }
}

impl From<Status> for i32 {
    fn from(s: Status) -> i32 {
        s.0 as i32
    }
}

impl std::fmt::Debug for Status {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        static NAMES: [&str; 4] = ["ATTACHED", "READY", "FINISHED", "ERROR"];
        let mut values = Vec::new();
        for (idx, name) in NAMES.iter().enumerate() {
            if self.0 & (1 << idx) != 0 {
                values.push(*name);
            }
        }
        if values.is_empty() {
            values.push(&"UNKNOWN");
        }
        write!(f, "{}", (*values).join("|"))
    }
}

impl Status {
    /// Checks if self contains the given status.
    pub fn contains(&self, other: Status) -> bool {
        assert!(other.0 != 0);
        self.0 & other.0 != 0
    }

    /// Marks `self' as containing the provided status.
    pub fn add(&mut self, other: Status) {
        self.0 |= other.0
    }

    /// Removes the given status from the set of statuses in `self'.
    pub fn remove(&mut self, other: Status) {
        self.0 &= !other.0
    }
}
