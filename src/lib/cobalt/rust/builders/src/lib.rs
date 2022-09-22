// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Helpers for building MetricEvent objects

pub mod metric_event_builder;

pub use metric_event_builder::MetricEventExt;
