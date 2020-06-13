// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::hooks::EventType, anyhow::Error, clonable_error::ClonableError,
    cm_rust::CapabilityName, thiserror::Error,
};

#[derive(Debug, Error, Clone)]
pub enum EventsError {
    #[error("Registry not found")]
    RegistryNotFound,

    #[error("Events not allowed for subscription {:?}", names)]
    NotAvailable { names: Vec<CapabilityName> },

    #[error("Filter is not a subset")]
    InvalidFilter,

    #[error("Event routes must end at source with a filter declaration")]
    MissingFilter,

    #[error("Subscribe failed: {}", err)]
    SynthesisFailed {
        #[source]
        err: ClonableError,
    },

    #[error("Cannot transfer event: {}", event_type)]
    CannotTransfer { event_type: EventType },
}

impl EventsError {
    pub fn not_available(names: Vec<CapabilityName>) -> Self {
        Self::NotAvailable { names }
    }

    pub fn synthesis_failed(err: impl Into<Error>) -> Self {
        Self::SynthesisFailed { err: err.into().into() }
    }

    pub fn cannot_transfer(event_type: EventType) -> Self {
        Self::CannotTransfer { event_type }
    }
}
