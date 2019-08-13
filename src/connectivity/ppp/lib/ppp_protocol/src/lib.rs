// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for implementing PPP control protocols.
//!
//! Provides a generic implementation of an LCP-like protocol state machine, and implementations of
//! LCP, IPCP and IPV6CP using this generic implementation.

#![feature(async_await)]
#![deny(missing_docs)]

pub mod ppp;

pub mod ipv4;
pub mod ipv6;
pub mod link;

mod test;
