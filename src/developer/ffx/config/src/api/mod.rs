// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::EnvironmentContext;
use anyhow::Result;
use serde_json::Value;
use std::convert::{From, TryFrom, TryInto};
use thiserror::Error;

pub mod query;
pub mod value;

pub type ConfigResult = Result<ConfigValue>;
pub use query::{BuildOverride, ConfigQuery};
pub use value::ConfigValue;

#[derive(Debug, Error)]
#[error("Configuration error")]
pub struct ConfigError(#[from] anyhow::Error);

impl ConfigError {
    pub fn new(e: anyhow::Error) -> Self {
        Self(e)
    }
}

pub(crate) fn validate_type<T>(_ctx: &EnvironmentContext, value: Value) -> Option<Value>
where
    T: TryFrom<ConfigValue>,
    <T as std::convert::TryFrom<ConfigValue>>::Error: std::convert::From<ConfigError>,
{
    let result: std::result::Result<T, T::Error> = ConfigValue(Some(value.clone())).try_into();
    match result {
        Ok(_) => Some(value),
        Err(_) => None,
    }
}

impl From<ConfigError> for std::convert::Infallible {
    fn from(_value: ConfigError) -> Self {
        panic!("never going to happen")
    }
}
