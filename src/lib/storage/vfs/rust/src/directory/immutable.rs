// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Directories that live in this module are "immutable" from the client standpoint.  Even over a
//! connection with the write access, clients may not rename, delete or link entries in these
//! directories.

pub mod simple;
pub use simple::{simple, Simple};

pub mod lazy;
pub use lazy::{lazy, Lazy};

pub mod connection;
