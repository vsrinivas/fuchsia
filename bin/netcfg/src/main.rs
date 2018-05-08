// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate failure;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;

use failure::{Error};

const STATIC_CONFIG: &str = "{ \"hostname\": \"fuchsia\" }";

#[derive(Debug, Deserialize)]
pub struct Config {
    pub hostname: String,
}

fn main() -> Result<(), Error> {
    let res: Result<Config, Error> = serde_json::from_str(STATIC_CONFIG).map_err(Into::into);
    println!("{:?}", res);
    Ok(())
}
