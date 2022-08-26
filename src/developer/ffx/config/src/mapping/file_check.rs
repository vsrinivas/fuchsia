// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::EnvironmentContext;
use serde_json::Value;
use std::path::PathBuf;

/// Filters for config values that map to files that are reachable. Returns None
/// for strings that don't correspond to files discoverable by [`PathBuf::exists`],
/// but maps to the same value for anything else.
pub(crate) fn file_check(_ctx: &EnvironmentContext, value: Value) -> Option<Value> {
    match &value {
        Value::String(s) if PathBuf::from(s).exists() => Some(value),
        Value::String(_) => None, // filter out strings that don't correspond to existing files.
        _ => Some(value),         // but let any other type through.
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::ConfigMap;
    use anyhow::{bail, Result};
    use serde_json::json;
    use tempfile::NamedTempFile;

    #[test]
    fn test_file_mapper() -> Result<()> {
        let ctx = EnvironmentContext::isolated("/tmp".into(), ConfigMap::default(), None);
        let file = NamedTempFile::new()?;
        if let Some(path) = file.path().to_str() {
            let test = Value::String(path.to_string());
            assert_eq!(file_check(&ctx, test), Some(Value::String(path.to_string())));
            Ok(())
        } else {
            bail!("Unable to get temp file path");
        }
    }

    #[test]
    fn test_file_mapper_returns_none() -> Result<()> {
        let ctx = EnvironmentContext::isolated("/tmp".into(), ConfigMap::default(), None);
        let test = json!("/fake_path/should_not_exist");
        assert_eq!(file_check(&ctx, test), None);
        Ok(())
    }
}
