// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fragment_io::END_OF_MSG;
use std::collections::BTreeMap;

const MAX_REASSEMBLING_FRAMES: usize = 3;

#[derive(PartialEq, Debug)]
enum Frame {
    Assembling(BTreeMap<u8, Vec<u8>>),
    Delivered,
}

impl Default for Frame {
    fn default() -> Self {
        Frame::Assembling(BTreeMap::new())
    }
}

pub struct Reassembler {
    frames: BTreeMap<u64, Frame>,
    largest_seen_msg_id: u64,
}

impl Reassembler {
    pub fn new() -> Reassembler {
        Reassembler { frames: BTreeMap::new(), largest_seen_msg_id: 256 }
    }

    pub fn recv(&mut self, msg_id: u8, fragment_id: u8, fragment: Vec<u8>) -> Option<Vec<u8>> {
        let msg_id = self.expand_msg_id(msg_id);
        let frame = self.frames.entry(msg_id).or_default();
        let mut out = None;
        if let Frame::Assembling(frame) = frame {
            frame.insert(fragment_id, fragment);
            let last_fragment_id = frame.keys().next_back().unwrap();
            if last_fragment_id & END_OF_MSG == END_OF_MSG
                && frame.len() == 1 + ((last_fragment_id & !END_OF_MSG) as usize)
            {
                out = Some(
                    frame
                        .into_iter()
                        .map(|(_, v)| std::mem::replace(v, Vec::new()))
                        .flatten()
                        .collect(),
                );
                self.frames.insert(msg_id, Frame::Delivered);
            }
        }
        while self.frames.len() > MAX_REASSEMBLING_FRAMES {
            let first = *self.frames.keys().next().unwrap();
            self.frames.remove(&first);
        }
        out
    }

    fn expand_msg_id(&mut self, msg_id: u8) -> u64 {
        let msg_id = msg_id as u64;
        let expected_id = self.largest_seen_msg_id + 1;
        let candidate_id = (expected_id & !0xff) | msg_id;
        let msg_id = if candidate_id <= expected_id - 128 {
            candidate_id + 256
        } else if candidate_id > expected_id + 128 {
            candidate_id - 256
        } else {
            candidate_id
        };
        if msg_id > self.largest_seen_msg_id {
            self.largest_seen_msg_id = msg_id;
        }
        msg_id
    }
}

#[cfg(test)]
mod test {

    use super::*;

    #[test]
    fn expand_msg_id() {
        let mut r = Reassembler::new();
        assert_eq!(r.expand_msg_id(0), 256);
        assert_eq!(r.expand_msg_id(0), 256);
        assert_eq!(r.expand_msg_id(1), 257);
        assert_eq!(r.expand_msg_id(2), 258);
        assert_eq!(r.expand_msg_id(3), 259);
        assert_eq!(r.expand_msg_id(4), 260);
        assert_eq!(r.expand_msg_id(0), 256);
        assert_eq!(r.expand_msg_id(1), 257);
        assert_eq!(r.expand_msg_id(2), 258);
        assert_eq!(r.expand_msg_id(3), 259);
        assert_eq!(r.expand_msg_id(4), 260);
        for i in 0u64..4096u64 {
            assert_eq!(r.expand_msg_id((i % 256) as u8), i + 256);
        }
    }

    #[test]
    fn single_frame() {
        let mut r = Reassembler::new();
        assert_eq!(r.recv(1, 0 | END_OF_MSG, vec![1, 2, 3]), Some(vec![1, 2, 3]));
        for frame in r.frames.values() {
            assert_eq!(Frame::Delivered, *frame);
        }
    }

    #[test]
    fn multiple_ordered_fragments() {
        let mut r = Reassembler::new();
        assert_eq!(r.recv(1, 0, vec![1, 2, 3]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 2 | END_OF_MSG, vec![7, 8, 9]), Some(vec![1, 2, 3, 4, 5, 6, 7, 8, 9]));
    }

    #[test]
    fn repeated_fragment() {
        let mut r = Reassembler::new();
        assert_eq!(r.recv(1, 0, vec![1, 2, 3]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 2 | END_OF_MSG, vec![7, 8, 9]), Some(vec![1, 2, 3, 4, 5, 6, 7, 8, 9]));
    }

    #[test]
    fn multiple_unordered_fragments_0() {
        let mut r = Reassembler::new();
        assert_eq!(r.recv(1, 0, vec![1, 2, 3]), None);
        assert_eq!(r.recv(1, 2 | END_OF_MSG, vec![7, 8, 9]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), Some(vec![1, 2, 3, 4, 5, 6, 7, 8, 9]));
    }

    #[test]
    fn multiple_unordered_fragments_1() {
        let mut r = Reassembler::new();
        assert_eq!(r.recv(1, 2 | END_OF_MSG, vec![7, 8, 9]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 0, vec![1, 2, 3]), Some(vec![1, 2, 3, 4, 5, 6, 7, 8, 9]));
    }

    #[test]
    fn multiple_concurrent_fragments() {
        let mut r = Reassembler::new();
        assert_eq!(r.recv(1, 0, vec![1, 2, 3]), None);
        assert_eq!(r.recv(2, 2 | END_OF_MSG, vec![7, 8, 9]), None);
        assert_eq!(r.recv(1, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(2, 1, vec![4, 5, 6]), None);
        assert_eq!(r.recv(1, 2 | END_OF_MSG, vec![7, 8, 9]), Some(vec![1, 2, 3, 4, 5, 6, 7, 8, 9]));
        assert_eq!(r.recv(2, 0, vec![1, 2, 3]), Some(vec![1, 2, 3, 4, 5, 6, 7, 8, 9]));
    }
}
