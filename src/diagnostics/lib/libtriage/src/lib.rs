// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod act; // Perform appropriate actions.
pub mod config; // Read the config file(s) for metric and action specs.
pub mod metrics; // Retrieve and calculate the metrics.
pub mod result_format; // Formats the triage results.
pub mod validate; // Check config - including that metrics/triggers work correctly.
