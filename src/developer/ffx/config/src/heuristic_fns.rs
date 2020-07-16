// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::constants::{SSH_PRIV, SSH_PUB},
    serde_json::Value,
    std::path::Path,
};

pub(crate) fn find_ssh_keys(key: &str) -> Option<Value> {
    let k = if key == SSH_PUB { "authorized_keys" } else { "pkey" };
    if key == SSH_PRIV {
        match std::env::var("FUCHSIA_SSH_KEY") {
            Ok(r) => {
                if Path::new(&r).exists() {
                    return Some(Value::String(r));
                }
            }
            Err(_) => {}
        }
    }
    match std::env::var("FUCHSIA_DIR") {
        Ok(r) => {
            if Path::new(&r).exists() {
                return Some(Value::String(String::from(format!("{}/.ssh/{}", r, k))));
            }
        }
        Err(_) => {
            if key != SSH_PUB {
                return None;
            }
        }
    }
    match std::env::var("HOME") {
        Ok(r) => {
            if Path::new(&r).exists() {
                Some(Value::String(String::from(format!("{}/.ssh/id_rsa.pub", r))))
            } else {
                None
            }
        }
        Err(_) => None,
    }
}
