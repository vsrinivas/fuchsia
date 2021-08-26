// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{container::ComponentIdentity, events::types::ComponentIdentifier};

use super::stats::LogStreamStats;
use diagnostics_data::{BuilderArgs, LogsData, LogsDataBuilder};
use diagnostics_message::{
    error::MessageError,
    message::{Message, MonikerWithUrl},
};
use lazy_static::lazy_static;
use serde::{Serialize, Serializer};
use std::{
    cmp::Ordering,
    ops::{Deref, DerefMut},
    sync::Arc,
};

#[derive(Clone)]
pub struct MessageWithStats {
    msg: diagnostics_message::message::Message,
    stats: Arc<LogStreamStats>,
}

impl MessageWithStats {
    /// Returns a new Message which encodes a count of dropped messages in its metadata.
    pub fn for_dropped(count: u64, source: MonikerWithUrl, timestamp: i64) -> Self {
        let message = format!("Rolled {} logs out of buffer", count);
        MessageWithStats::from(
            LogsDataBuilder::new(BuilderArgs {
                moniker: source.moniker,
                timestamp_nanos: timestamp.into(),
                component_url: Some(source.url.clone()),
                severity: diagnostics_data::Severity::Warn,
                size_bytes: 0,
            })
            .add_error(diagnostics_data::LogError::DroppedLogs { count })
            .set_message(message)
            .build(),
        )
    }

    pub(crate) fn with_stats(mut self, stats: &Arc<LogStreamStats>) -> Self {
        self.stats = stats.clone();
        self
    }

    pub fn from_logger(source: &ComponentIdentity, bytes: &[u8]) -> Result<Self, MessageError> {
        let msg = Message::from_logger(MonikerWithUrl::from(source), &bytes)?;
        Ok(MessageWithStats::from(msg))
    }

    pub fn from_structured(source: &ComponentIdentity, bytes: &[u8]) -> Result<Self, MessageError> {
        let msg = Message::from_structured(MonikerWithUrl::from(source), &bytes)?;
        Ok(MessageWithStats::from(msg))
    }
}

impl Drop for MessageWithStats {
    fn drop(&mut self) {
        self.stats.increment_dropped(&*self);
    }
}

impl From<ComponentIdentity> for MonikerWithUrl {
    fn from(identity: ComponentIdentity) -> Self {
        Self { moniker: identity.to_string(), url: identity.url }
    }
}

impl From<&ComponentIdentity> for MonikerWithUrl {
    fn from(identity: &ComponentIdentity) -> Self {
        Self { moniker: identity.to_string(), url: identity.url.clone() }
    }
}

impl std::fmt::Debug for MessageWithStats {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.msg.fmt(f)
    }
}

impl Serialize for MessageWithStats {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        self.msg.serialize(serializer)
    }
}

impl From<LogsData> for MessageWithStats {
    fn from(data: LogsData) -> Self {
        Self { msg: diagnostics_message::message::Message::from(data), stats: Default::default() }
    }
}

impl From<diagnostics_message::message::Message> for MessageWithStats {
    fn from(msg: diagnostics_message::message::Message) -> Self {
        Self { msg, stats: Default::default() }
    }
}

impl Ord for MessageWithStats {
    fn cmp(&self, other: &Self) -> Ordering {
        self.msg.cmp(&other.msg)
    }
}

impl PartialEq for MessageWithStats {
    fn eq(&self, rhs: &Self) -> bool {
        self.msg.eq(&rhs.msg)
    }
}

lazy_static! {
    pub static ref EMPTY_IDENTITY: ComponentIdentity = ComponentIdentity::unknown();
    pub static ref TEST_IDENTITY: Arc<ComponentIdentity> = {
        Arc::new(ComponentIdentity::from_identifier_and_url(
            &ComponentIdentifier::Legacy {
                moniker: vec!["fake-test-env", "test-component.cmx"].into(),
                instance_id: "".into(),
            },
            "fuchsia-pkg://fuchsia.com/testing123#test-component.cmx",
        ))
    };
}

impl PartialOrd for MessageWithStats {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(&other))
    }
}

impl Eq for MessageWithStats {}

impl Deref for MessageWithStats {
    type Target = diagnostics_message::message::Message;
    fn deref(&self) -> &Self::Target {
        &self.msg
    }
}

impl DerefMut for MessageWithStats {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.msg
    }
}
