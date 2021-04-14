// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::Cell, fmt};

use crate::{core::BinaryReader, shapes::paint::Color32};

#[derive(Default)]
pub struct Property<T> {
    cell: Cell<T>,
}

impl<T: Clone + Default> Property<T> {
    pub fn new(val: T) -> Self {
        Self { cell: Cell::new(val) }
    }

    pub fn get(&self) -> T {
        let val = self.cell.take();
        self.cell.set(val.clone());
        val
    }

    pub fn set(&self, val: T) {
        self.cell.set(val);
    }
}

impl<T: Clone + fmt::Debug + Default> fmt::Debug for Property<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Property").field("value", &self.get()).finish()
    }
}

pub trait TryFromU64: Sized {
    fn try_from(val: u64) -> Option<Self>;
}

pub trait Writable {
    fn write(&self, reader: &mut BinaryReader<'_>) -> Option<()>;
}

impl Writable for Property<bool> {
    fn write(&self, reader: &mut BinaryReader<'_>) -> Option<()> {
        self.set(reader.read_u8()? != 0);
        Some(())
    }
}

impl Writable for Property<u32> {
    fn write(&self, reader: &mut BinaryReader<'_>) -> Option<()> {
        self.set(reader.read_u32()?);
        Some(())
    }
}

impl Writable for Property<u64> {
    fn write(&self, reader: &mut BinaryReader<'_>) -> Option<()> {
        self.set(reader.read_var_u64()?);
        Some(())
    }
}

impl Writable for Property<f32> {
    fn write(&self, reader: &mut BinaryReader<'_>) -> Option<()> {
        self.set(reader.read_f32()?);
        Some(())
    }
}

impl Writable for Property<Color32> {
    fn write(&self, reader: &mut BinaryReader<'_>) -> Option<()> {
        self.set(reader.read_u32()?.into());
        Some(())
    }
}

impl Writable for Property<String> {
    fn write(&self, reader: &mut BinaryReader<'_>) -> Option<()> {
        self.set(reader.read_string()?);
        Some(())
    }
}

impl<T: TryFromU64 + Clone + Default> Writable for Property<T> {
    fn write(&self, reader: &mut BinaryReader<'_>) -> Option<()> {
        self.set(TryFromU64::try_from(reader.read_var_u64()?)?);
        Some(())
    }
}
