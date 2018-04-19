// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::task::{Context, Waker};
use mesh_protocol;
use std::{cmp::Ordering, collections::HashMap};

#[derive(Debug, Fail)]
pub enum OrderingError {
    #[fail(display = "Old message ({}) received from {:?}:{}", seq, peer, stream_id)]
    OldMessageReceived {
        peer: mesh_protocol::NodeId,
        stream_id: u64,
        seq: u64,
    },
    #[fail(display = "Duplicate message ({}) received from {:?}:{}", seq, peer, stream_id)]
    DuplicateMessageReceived {
        peer: mesh_protocol::NodeId,
        stream_id: u64,
        seq: u64,
    },
}

const UNORDERED_WINDOW_SIZE: u64 = 64;

pub enum StreamType {
    ReliableOrdered {
        tip: u64,
        pending: HashMap<u64, Waker>,
    },
    ReliableUnordered {
        tip: u64,
        seen_mask: u64,
        pending: HashMap<u64, Waker>,
    },
    UnreliableOrdered {
        tip: u64,
    },
    UnreliableUnordered {
        tip: u64,
        seen_mask: u64,
    },
    LastMessageReliable {
        tip: u64,
    },
}

pub enum PollResult {
    Accept,
    Wait { nack: Vec<u64> },
    Err(OrderingError),
}

impl StreamType {
    pub fn new(stream_type: mesh_protocol::StreamType) -> StreamType {
        match stream_type {
            mesh_protocol::StreamType::ReliableOrdered => StreamType::ReliableOrdered {
                tip: 0,
                pending: HashMap::new(),
            },
            mesh_protocol::StreamType::ReliableUnordered => StreamType::ReliableUnordered {
                tip: 0,
                seen_mask: 0,
                pending: HashMap::new(),
            },
            mesh_protocol::StreamType::UnreliableOrdered => {
                StreamType::UnreliableOrdered { tip: 0 }
            }
            mesh_protocol::StreamType::UnreliableUnordered => StreamType::UnreliableUnordered {
                tip: 0,
                seen_mask: 0,
            },
            mesh_protocol::StreamType::LastMessageReliable => {
                StreamType::LastMessageReliable { tip: 0 }
            }
        }
    }

    pub fn rmp_stream_type(&self) -> mesh_protocol::StreamType {
        match self {
            StreamType::ReliableOrdered { .. } => mesh_protocol::StreamType::ReliableOrdered,
            StreamType::ReliableUnordered { .. } => mesh_protocol::StreamType::ReliableUnordered,
            StreamType::UnreliableOrdered { .. } => mesh_protocol::StreamType::UnreliableOrdered,
            StreamType::UnreliableUnordered { .. } => {
                mesh_protocol::StreamType::UnreliableUnordered
            }
            StreamType::LastMessageReliable { .. } => {
                mesh_protocol::StreamType::LastMessageReliable
            }
        }
    }

    pub fn poll_impl(
        &mut self, cx: &mut Context, seq: u64, peer: mesh_protocol::NodeId, stream_id: u64,
    ) -> PollResult {
        match self {
            StreamType::ReliableOrdered { tip, pending } => match seq.cmp(tip) {
                Ordering::Equal => {
                    pending.remove(tip);
                    *tip += 1;
                    if let Some(waker) = pending.remove(tip) {
                        waker.wake();
                    }
                    PollResult::Accept
                }
                Ordering::Greater => {
                    pending.insert(seq, cx.waker().clone());
                    PollResult::Wait {
                        nack: (*tip..seq).filter(|n| !pending.contains_key(n)).collect(),
                    }
                }
                Ordering::Less => PollResult::Err(OrderingError::OldMessageReceived {
                    peer,
                    stream_id,
                    seq,
                }),
            },
            StreamType::ReliableUnordered { tip, seen_mask, pending } => match seq.cmp(tip) {
                Ordering::Equal => {
                    let out = if *seen_mask & 1 != 1 {
                        PollResult::Accept
                    } else {
                        PollResult::Err(OrderingError::DuplicateMessageReceived {
                            peer,
                            stream_id,
                            seq,
                        })
                    };
                    if let Some(waker) = pending.remove(&(*tip + UNORDERED_WINDOW_SIZE)) {
                        waker.wake();
                    }
                    *tip += 1;
                    *seen_mask >>= 1;
                    out
                }
                Ordering::Greater => if seq < *tip + UNORDERED_WINDOW_SIZE {
                    let index = seq - *tip;
                    let mask = 1 << index;
                    if *seen_mask & mask != mask {
                        *seen_mask |= mask;
                        PollResult::Accept
                    } else {
                        PollResult::Err(OrderingError::DuplicateMessageReceived {
                            peer,
                            stream_id,
                            seq,
                        })
                    }
                } else {
                    pending.insert(seq, cx.waker().clone());
                    PollResult::Wait {
                        nack: (0..UNORDERED_WINDOW_SIZE)
                            .filter(|n| (*seen_mask & ((1 as u64) << n)) != 0)
                            .map(|n| n + *tip)
                            .chain(
                                (*tip + UNORDERED_WINDOW_SIZE..seq)
                                    .filter(|n| !pending.contains_key(n)),
                            )
                            .collect(),
                    }
                },
                Ordering::Less => PollResult::Err(OrderingError::OldMessageReceived {
                    peer,
                    stream_id,
                    seq,
                }),
            },
            StreamType::UnreliableOrdered { tip } => if *tip >= seq {
                *tip = seq + 1;
                PollResult::Accept
            } else {
                PollResult::Err(OrderingError::OldMessageReceived {
                    peer,
                    stream_id,
                    seq,
                })
            },
            StreamType::UnreliableUnordered { tip, seen_mask } => match seq.cmp(tip) {
                Ordering::Equal => {
                    let out = if *seen_mask & 1 != 1 {
                        PollResult::Accept
                    } else {
                        PollResult::Err(OrderingError::DuplicateMessageReceived {
                            peer,
                            stream_id,
                            seq,
                        })
                    };
                    *tip += 1;
                    *seen_mask >>= 1;
                    out
                }
                Ordering::Greater => if seq < *tip + UNORDERED_WINDOW_SIZE {
                    let index = seq - *tip;
                    let mask = 1 << index;
                    if *seen_mask & mask != mask {
                        *seen_mask |= 1;
                        PollResult::Accept
                    } else {
                        PollResult::Err(OrderingError::DuplicateMessageReceived {
                            peer,
                            stream_id,
                            seq,
                        })
                    }
                } else {
                    *tip = seq + 1;
                    *seen_mask = 0;
                    PollResult::Accept
                },
                Ordering::Less => PollResult::Err(OrderingError::OldMessageReceived {
                    peer,
                    stream_id,
                    seq,
                }),
            },
            StreamType::LastMessageReliable { tip } => if seq >= *tip {
                *tip = seq + 1;
                PollResult::Accept
            } else {
                PollResult::Err(
                    OrderingError::OldMessageReceived {
                        peer,
                        stream_id,
                        seq,
                    }.into(),
                )
            },
        }
    }
}
