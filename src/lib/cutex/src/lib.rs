// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Cutex - a Conditionally-acquired mUTEX

mod constants;
mod cutex;
mod list;
mod ticket;

pub use cutex::{AcquisitionPredicate, Cutex, CutexGuard, CutexLockFuture};
pub use ticket::CutexTicket;
