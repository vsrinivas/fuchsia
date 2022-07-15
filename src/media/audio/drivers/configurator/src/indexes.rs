// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Eq, Hash, PartialEq, Clone, Debug)]
pub struct StreamConfigIndex {
    pub id: [u8; 16],
    pub is_input: bool,
}

pub const STREAM_CONFIG_INDEX_SPEAKERS: StreamConfigIndex =
    StreamConfigIndex { id: [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], is_input: false };

pub const STREAM_CONFIG_INDEX_MICS: StreamConfigIndex =
    StreamConfigIndex { id: [3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], is_input: true };

pub const STREAM_CONFIG_INDEX_HEADSET_OUT: StreamConfigIndex =
    StreamConfigIndex { id: [2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], is_input: false };

pub const STREAM_CONFIG_INDEX_HEADSET_IN: StreamConfigIndex =
    StreamConfigIndex { id: [2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], is_input: true };
