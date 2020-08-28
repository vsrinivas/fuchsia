// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ConfigLevel, anyhow::Result, serde_json::Value, std::fmt};

pub type ConfigMapper = fn(Value) -> Option<Value>;

pub trait ReadConfig: fmt::Display {
    fn get(&self, key: &str, mapper: ConfigMapper) -> Option<Value>;
}

pub trait WriteConfig {
    fn set(&mut self, level: &ConfigLevel, key: &str, value: Value) -> Result<()>;
    fn remove(&mut self, level: &ConfigLevel, key: &str) -> Result<()>;
}

pub trait PersistentConfig {
    fn save(
        &self,
        global: &Option<String>,
        build: &Option<&String>,
        user: &Option<String>,
    ) -> Result<()>;
}
