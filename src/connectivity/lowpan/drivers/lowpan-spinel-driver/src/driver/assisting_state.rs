// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use core::num::NonZeroU16;
use std::net::Ipv6Addr;
use std::time::{Duration, Instant};

/// Contains the state associated with assisting/commissioning
/// new devices onto the network.
#[derive(Debug, Clone, Eq, PartialEq)]
pub(super) struct AssistingState {
    pub qualification_rule: Ipv6PacketMatcherRule,
    pub active_sessions: Ipv6PacketMatcher,
    pub expiration: Instant,
}

impl Default for AssistingState {
    fn default() -> Self {
        AssistingState {
            qualification_rule: Default::default(),
            active_sessions: Default::default(),
            expiration: Instant::now(),
        }
    }
}

impl AssistingState {
    pub fn should_route_to_insecure(&self, packet: &[u8]) -> bool {
        self.is_assisting() && return self.active_sessions.match_outbound_packet(packet)
    }

    pub fn should_allow_from_insecure(&mut self, packet: &[u8]) -> bool {
        if self.is_assisting() {
            if self.active_sessions.match_inbound_packet(packet) {
                return true;
            } else if self.qualification_rule.match_inbound_packet(packet) {
                self.active_sessions.update_with_inbound_packet(packet).unwrap();
                return true;
            }
        }
        false
    }

    pub fn update_from_secure(&mut self, packet: &[u8]) -> bool {
        if self.is_assisting() {
            return self.active_sessions.clear_with_inbound_packet(packet);
        }
        return false;
    }

    pub fn is_assisting(&self) -> bool {
        self.expiration > Instant::now()
    }

    pub fn clear(&mut self) {
        *self = AssistingState::default();
    }

    pub fn prepare_to_assist<T: Into<Duration>>(&mut self, duration: T, port: NonZeroU16) {
        let duration = duration.into();

        self.clear();

        if duration.as_nanos() > 0 {
            self.expiration += duration;
            self.qualification_rule = Ipv6PacketMatcherRule {
                local_port: Some(port),
                local_address: Subnet {
                    addr: Ipv6Addr::new(0xfe80, 0, 0, 0, 0, 0, 0, 0),
                    prefix_len: STD_IPV6_NET_PREFIX_LEN,
                },
                remote_address: Subnet {
                    addr: Ipv6Addr::new(0xfe80, 0, 0, 0, 0, 0, 0, 0),
                    prefix_len: STD_IPV6_NET_PREFIX_LEN,
                },
                ..Ipv6PacketMatcherRule::default()
            };
        }
    }
}
