// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ConfigLevel,
    std::{convert::From, default::Default},
};

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum SelectMode {
    First,
    All,
}

pub struct ConfigQuery<'a> {
    pub name: Option<&'a str>,
    pub level: Option<ConfigLevel>,
    pub build_dir: Option<&'a str>,
    pub select: SelectMode,
}

impl<'a> ConfigQuery<'a> {
    pub fn new(
        name: Option<&'a str>,
        level: Option<ConfigLevel>,
        build_dir: Option<&'a str>,
        select: SelectMode,
    ) -> Self {
        Self { name, level, build_dir, select }
    }
}

impl<'a> Default for ConfigQuery<'a> {
    fn default() -> Self {
        Self { name: None, level: None, build_dir: None, select: SelectMode::First }
    }
}

impl<'a> From<&'a str> for ConfigQuery<'a> {
    fn from(value: &'a str) -> Self {
        ConfigQuery { name: Some(value), ..Default::default() }
    }
}

impl<'a> From<&'a String> for ConfigQuery<'a> {
    fn from(value: &'a String) -> Self {
        ConfigQuery { name: Some(&value), ..Default::default() }
    }
}

impl<'a> From<ConfigLevel> for ConfigQuery<'a> {
    fn from(value: ConfigLevel) -> Self {
        ConfigQuery { level: Some(value), ..Default::default() }
    }
}

impl<'a> From<(&'a str, ConfigLevel)> for ConfigQuery<'a> {
    fn from(value: (&'a str, ConfigLevel)) -> Self {
        ConfigQuery { name: Some(value.0), level: Some(value.1), ..Default::default() }
    }
}

impl<'a> From<(&'a str, &ConfigLevel)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &ConfigLevel)) -> Self {
        ConfigQuery { name: Some(value.0), level: Some(*value.1), ..Default::default() }
    }
}

impl<'a> From<(&'a str, &'a str)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &'a str)) -> Self {
        ConfigQuery { name: Some(value.0), build_dir: Some(value.1), ..Default::default() }
    }
}

impl<'a> From<(&'a str, &ConfigLevel, &'a str)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &ConfigLevel, &'a str)) -> Self {
        ConfigQuery {
            name: Some(value.0),
            level: Some(*value.1),
            build_dir: Some(value.2),
            ..Default::default()
        }
    }
}

impl<'a> From<(&'a str, &'a ConfigLevel, &'a Option<String>)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &'a ConfigLevel, &'a Option<String>)) -> Self {
        ConfigQuery {
            name: Some(value.0),
            level: Some(*value.1),
            build_dir: value.2.as_ref().map(|s| s.as_str()),
            ..Default::default()
        }
    }
}

impl<'a> From<(&'a String, &'a ConfigLevel, &'a Option<String>)> for ConfigQuery<'a> {
    fn from(value: (&'a String, &'a ConfigLevel, &'a Option<String>)) -> Self {
        ConfigQuery {
            name: Some(value.0.as_str()),
            level: Some(*value.1),
            build_dir: value.2.as_ref().map(|s| s.as_str()),
            ..Default::default()
        }
    }
}

impl<'a> From<(&'a str, &'a SelectMode)> for ConfigQuery<'a> {
    fn from(value: (&'a str, &'a SelectMode)) -> Self {
        ConfigQuery { name: Some(value.0), select: *value.1, ..Default::default() }
    }
}

impl<'a> From<(&'a String, &'a SelectMode)> for ConfigQuery<'a> {
    fn from(value: (&'a String, &'a SelectMode)) -> Self {
        ConfigQuery { name: Some(value.0), select: *value.1, ..Default::default() }
    }
}
