// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cpu;
#[cfg(feature = "gpu")]
mod gpu;

pub use cpu::CpuRenderer;
#[cfg(feature = "gpu")]
pub use gpu::GpuRenderer;
