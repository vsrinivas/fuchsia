// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Helpers for building CobaltEvent and MetricEvent objects

pub mod cobalt_event_builder;
pub mod metric_event_builder;

pub use {cobalt_event_builder::CobaltEventExt, metric_event_builder::MetricEventExt};
