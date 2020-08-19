// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cobalt;
mod inspect;

#[cfg(test)]
pub use self::cobalt::fake::{FakeCobaltDiagnostics, FakeCobaltMonitor};
pub use self::cobalt::{CobaltDiagnostics, CobaltDiagnosticsImpl};
pub use self::inspect::{InspectDiagnostics, INSPECTOR};
