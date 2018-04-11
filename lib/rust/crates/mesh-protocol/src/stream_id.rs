// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use varint;

#[derive(PartialEq, PartialOrd, Clone, Eq, Hash, Debug, Copy)]
pub enum StreamType {
    ReliableOrdered,
    ReliableUnordered,
    UnreliableOrdered,
    UnreliableUnordered,
    LastMessageReliable,
}

// Stream identifier
#[derive(PartialEq, PartialOrd, Clone, Eq, Hash, Debug, Copy)]
pub struct StreamId {
    id: u64,
    stream_type: StreamType,
}

impl StreamId {
    pub fn writer(&self) -> varint::Writer {
        varint::Writer::new(
            (self.id << 3) + match self.stream_type {
                StreamType::ReliableOrdered => 0,
                StreamType::ReliableUnordered => 1,
                StreamType::UnreliableOrdered => 2,
                StreamType::UnreliableUnordered => 3,
                StreamType::LastMessageReliable => 4,
            },
        )
    }

    pub fn new(id: u64, stream_type: StreamType) -> StreamId {
        StreamId { id, stream_type }
    }

    pub fn index(&self) -> u64 {
        self.id
    }

    pub fn stream_type(&self) -> StreamType {
        self.stream_type
    }
}
