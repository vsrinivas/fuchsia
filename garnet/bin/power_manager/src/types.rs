// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Below are definitions for common types that may be used across Node boundaries (i.e.,
/// arguments within Messages). It may also be useful to define newtypes here to avoid
/// carrying around implicit unit measurement information (e.g., Celsius(f64)).

/// A newtype struct to represent degrees Celsius implemented by an underlying f64
#[derive(Debug)]
pub struct Celsius(pub f64);
