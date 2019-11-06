// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use async_trait::async_trait;
use failure::{format_err, Error};
use std::rc::Rc;

/// Node: CpuStatsHandler
///
/// Summary: WIP
///
/// Handles Messages: WIP
///
/// Sends Messages: WIP
///
/// FIDL: WIP

pub struct CpuStatsHandler;

impl CpuStatsHandler {
    pub fn new() -> Rc<Self> {
        Rc::new(Self)
    }
}

#[async_trait(?Send)]
impl Node for CpuStatsHandler {
    fn name(&self) -> &'static str {
        "CpuStatsHandler"
    }

    async fn handle_message(&self, msg: &Message<'_>) -> Result<MessageReturn, Error> {
        match msg {
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}
