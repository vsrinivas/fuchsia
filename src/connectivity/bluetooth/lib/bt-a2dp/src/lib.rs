// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Describing and encapsulating A2DP media codecs
pub mod media_types;

/// Codec configuration and integration with Fuchsia media
pub mod codec;

/// Task Abstractions
pub mod media_task;

/// Handling AVDTP streams
pub mod stream;

/// Structures to support inspection
pub mod inspect;

/// Peer tracking and signaling procedures
pub mod peer;

/// Collections of connected peers
pub mod connected_peers;

/// Real-time Transport Protocol parsing and packet building
pub mod rtp;
