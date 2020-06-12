// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Describing and enxapsulating A2DP media codecs
pub mod media_types;

/// Codec configuration and integraion with Fuchsia media
pub mod codec;

/// Task Abstractions
pub mod media_task;

/// Handling AVDTP streams
pub mod stream;

/// Structures to support inspection
pub mod inspect;
