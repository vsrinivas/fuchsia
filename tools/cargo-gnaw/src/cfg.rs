// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};

/// Convert a cargo cfg conditional into GN imperative control flow
// TODO This should consume some information in the Cargo.toml file
// to establish conventions for the carge -> GN build. This is hardcoded
// to support Fuchsia at the moment.
//
// wow. an interview question in real life.
pub fn cfg_to_gn_conditional(cfg: &str) -> Result<String, Error> {
    if cfg.starts_with("cfg") {
        Ok(cfg_to_gn_conditional(&cfg[4..cfg.len() - 1])?)
    } else if cfg.starts_with("not") {
        let section = &cfg[4..];
        let mut paren_count = 1;
        for (idx, c) in section.chars().enumerate() {
            if c == ')' {
                paren_count -= 1;
                if paren_count == 0 {
                    return Ok(format!("!({})", cfg_to_gn_conditional(&section[..idx])?));
                }
            } else if c == '(' {
                paren_count += 1;
            }
        }
        Err(anyhow!("bad not statement"))
    } else if cfg.starts_with("any") {
        let section = &cfg[4..cfg.len()];
        let mut accum = vec![];
        let mut paren_count = 1;
        let mut start_idx = 0;
        for (idx, c) in section.chars().enumerate() {
            if c == ')' {
                paren_count -= 1;
                if paren_count == 0 {
                    accum.push(cfg_to_gn_conditional(&section[start_idx..idx])?);
                }
            } else if c == '(' {
                paren_count += 1;
            } else if c == ',' && paren_count <= 1 {
                accum.push(cfg_to_gn_conditional(&section[start_idx..idx])?);
                start_idx = idx + 2; // skip ", "
            }
        }
        Ok(format!("({})", accum.join(" || ")))
    } else if cfg.starts_with("all") {
        let section = &cfg[4..cfg.len()];
        let mut accum = vec![];
        let mut paren_count = 1;
        let mut start_idx = 0;
        for (idx, c) in section.chars().enumerate() {
            if c == ')' {
                paren_count -= 1;
                if paren_count == 0 {
                    accum.push(cfg_to_gn_conditional(&section[start_idx..idx])?);
                }
            } else if c == '(' {
                paren_count += 1;
            } else if c == ',' && paren_count <= 1 {
                accum.push(cfg_to_gn_conditional(&section[start_idx..idx])?);
                start_idx = idx + 2; // skip ", "
            }
        }
        Ok(format!("({})", accum.join(" && ")))
    } else if cfg == "target_os = \"fuchsia\"" {
        Ok(String::from("current_os == \"fuchsia\""))
    } else if cfg == "target_os = \"macos\"" {
        Ok(String::from("current_os == \"mac\""))
    } else if cfg == "target_os = \"linux\"" {
        Ok(String::from("current_os == \"linux\""))
    } else if cfg == "unix" {
        // all our platforms are unix
        Ok(String::from("true"))
    } else if cfg == "feature = \"std\"" {
        // need to detect std usage
        Ok(String::from("true"))
    } else if cfg == "target_arch = \"aarch64\"" {
        Ok(String::from("current_cpu == \"arm64\""))
    } else if cfg == "target_arch = \"x86_64\"" {
        Ok(String::from("current_cpu == \"x64\""))
    } else if cfg == "windows" {
        // don't support host builds on windows right now
        Ok(String::from("false"))

    // Everything below is random cfgs that we don't know anything about
    } else if cfg.starts_with("target_os") {
        Ok(String::from("false"))
    } else if cfg.starts_with("target_arch") {
        Ok(String::from("false"))
    } else if cfg.starts_with("target_env") {
        Ok(String::from("false"))
    } else if cfg.starts_with("target_feature") {
        Ok(String::from("false"))
    } else if cfg.starts_with("target_vendor") {
        Ok(String::from("false"))
    } else {
        Err(anyhow!("Unknown cfg option used: {}", cfg))
    }
}

#[test]
fn basic_fuchsia() {
    let cfg_str = r#"cfg(target_os = "fuchsia")"#;
    let output = cfg_to_gn_conditional(cfg_str).unwrap();
    assert_eq!(output, "current_os == \"fuchsia\"");
}

#[test]
fn conditonal_any() {
    let cfg_str = r#"cfg(any(target_os = "fuchsia", target_os = "macos"))"#;
    let output = cfg_to_gn_conditional(cfg_str).unwrap();
    assert_eq!(output, "(current_os == \"fuchsia\" || current_os == \"mac\")");
}

#[test]
fn conditonal_all() {
    let cfg_str = r#"cfg(all(target_os = "fuchsia", target_os = "macos"))"#;
    let output = cfg_to_gn_conditional(cfg_str).unwrap();
    assert_eq!(output, "(current_os == \"fuchsia\" && current_os == \"mac\")");
}

#[test]
fn conditonal_all_not() {
    let cfg_str = r#"cfg(all(target_os = "fuchsia", not(target_os = "macos")))"#;
    let output = cfg_to_gn_conditional(cfg_str).unwrap();
    assert_eq!(output, "(current_os == \"fuchsia\" && !(current_os == \"mac\"))");
}

#[test]
fn conditonal_fail() {
    let cfg_str = r#"cfg(everything(target_os = "fuchsia"))"#;
    let output = cfg_to_gn_conditional(cfg_str).unwrap_err();
    assert_eq!(output.to_string(), r#"Unknown cfg option used: everything(target_os = "fuchsia")"#);
}

#[test]
fn nested_cfgs() {
    let cfg_str =
        r#"cfg(all(any(target_arch = "x86_64", target_arch = "aarch64"), target_os = "hermit"))"#;
    let output = cfg_to_gn_conditional(cfg_str).unwrap();
    assert_eq!(
        output.to_string(),
        r#"((current_cpu == "x64" || current_cpu == "arm64") && false)"#
    );
}
