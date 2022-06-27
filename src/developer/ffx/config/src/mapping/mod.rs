// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    regex::{Captures, Regex},
    serde_json::Value,
    std::path::PathBuf,
};

mod cache;
mod config;
mod data;
mod env_var;
mod file_check;
mod filter;
mod flatten;
mod home;
mod runtime;

pub(crate) use self::home::home;
pub(crate) use cache::cache;
pub(crate) use config::config;
pub(crate) use data::data;
pub(crate) use env_var::env_var;
pub(crate) use file_check::file_check;
pub(crate) use filter::filter;
pub(crate) use flatten::flatten;
pub(crate) use runtime::runtime;

// Negative lookbehind (or lookahead for that matter) is not supported in Rust's regex.
// Instead, replace with this string - which hopefully will not be used by anyone in the
// configuration.  Insert joke here about how hope is not a strategy.
const TEMP_REPLACE: &str = "#<#ffx!!replace#>#";

fn preprocess(value: &Value) -> Option<String> {
    value.as_str().map(|s| s.to_string()).map(|s| s.replace("$$", TEMP_REPLACE))
}

fn postprocess(value: String) -> Value {
    Value::String(value.to_string().replace(TEMP_REPLACE, "$"))
}

fn replace_regex<T>(value: &String, regex: &Regex, replacer: T) -> String
where
    T: Fn(&str) -> Result<String>,
{
    regex
        .replace_all(value, |caps: &Captures<'_>| {
            // Skip the first one since that'll be the whole string.
            caps.iter()
                .skip(1)
                .map(|cap| cap.map(|c| replacer(c.as_str())))
                .fold(String::new(), |acc, v| if let Some(Ok(s)) = v { acc + &s } else { acc })
        })
        .into_owned()
}

fn replace<'a, P>(regex: &'a Regex, base_path: P, value: Value) -> Option<Value>
where
    P: Fn() -> Result<PathBuf> + Sync + Send + 'a,
{
    preprocess(&value)
        .as_ref()
        .map(|s| {
            replace_regex(s, regex, |v| {
                match base_path() {
                    Ok(p) => Ok(p.to_str().map_or(v.to_string(), |s| s.to_string())),
                    Err(_) => Ok(v.to_string()), //just pass through
                }
            })
        })
        .map(postprocess)
        .or(Some(value))
}
