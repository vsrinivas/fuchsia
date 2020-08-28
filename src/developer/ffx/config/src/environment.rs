// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ConfigLevel,
    anyhow::{Context, Result},
    serde::{Deserialize, Serialize},
    std::{
        collections::HashMap,
        fmt,
        fs::{File, OpenOptions},
        io::{BufReader, BufWriter, Read, Write},
    },
};

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct Environment {
    pub user: Option<String>,
    pub build: Option<HashMap<String, String>>,
    pub global: Option<String>,
}

impl Environment {
    fn load_from_reader<R: Read>(reader: R) -> Result<Self> {
        serde_json::from_reader::<R, Environment>(reader).context("reading environment from disk")
    }

    fn save_to_writer<W: Write>(&self, writer: W) -> Result<()> {
        serde_json::to_writer_pretty(writer, &self).context("writing environment to disk")
    }

    pub(crate) fn try_load(file: Option<&String>) -> Self {
        file.map_or_else(
            || Self { user: None, build: None, global: None },
            |f| {
                let reader = Environment::reader(f);
                if reader.is_err() {
                    Self { user: None, build: None, global: None }
                } else {
                    Environment::load_from_reader(reader.expect("environment file reader"))
                        .context("reading environment")
                        .unwrap_or_else(|_| Self { user: None, build: None, global: None })
                }
            },
        )
    }

    pub fn load(file: &str) -> Result<Self> {
        Environment::load_from_reader(Environment::reader(file)?)
    }

    fn reader(path: &str) -> Result<BufReader<File>> {
        File::open(path).context("opening file for read").map(BufReader::new)
    }

    fn writer(path: &str) -> Result<BufWriter<File>> {
        OpenOptions::new()
            .write(true)
            .truncate(true)
            .create(true)
            .open(path)
            .context("opening file for write")
            .map(BufWriter::new)
    }

    pub fn save(&self, file: &str) -> Result<()> {
        self.save_to_writer(Environment::writer(file)?)
    }

    fn display_user(&self) -> String {
        self.user.as_ref().map_or_else(|| format!(" User: none\n"), |u| format!(" User: {}\n", u))
    }

    fn display_build(&self) -> String {
        let mut res = format!(" Build:");
        match self.build.as_ref() {
            Some(m) => {
                if m.is_empty() {
                    res.push_str(&format!("  none\n"));
                }
                res.push_str(&format!("\n"));
                for (key, val) in m.iter() {
                    res.push_str(&format!("  {} => {}\n", key, val));
                }
            }
            None => {
                res.push_str(&format!("  none\n"));
            }
        }
        res
    }

    fn display_global(&self) -> String {
        self.global
            .as_ref()
            .map_or_else(|| format!(" Global: none\n"), |g| format!(" Global: {}\n", g))
    }

    pub fn display(&self, level: &Option<ConfigLevel>) -> String {
        level.map_or_else(
            || {
                let mut res = format!("\nEnvironment:\n");
                res.push_str(&self.display_user());
                res.push_str(&self.display_build());
                res.push_str(&self.display_global());
                res
            },
            |l| match l {
                ConfigLevel::User => self.display_user(),
                ConfigLevel::Build => self.display_build(),
                ConfigLevel::Global => self.display_global(),
                _ => format!(" This level is not saved in the environment file."),
            },
        )
    }
}

impl fmt::Display for Environment {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "{}", self.display(&None))
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    const ENVIRONMENT: &'static str = r#"
        {
            "user": "/tmp/user.json",
            "build": {
                "/tmp/build/1": "/tmp/build/1/build.json"
            },
            "global": "/tmp/global.json"
        }"#;

    #[test]
    fn test_loading_and_saving_environment() -> Result<()> {
        let mut env_file = String::from(ENVIRONMENT);
        let environment = Environment::load_from_reader(BufReader::new(env_file.as_bytes()))?;
        let mut env_file_out = String::new();

        unsafe {
            environment.save_to_writer(BufWriter::new(env_file_out.as_mut_vec()))?;
        }

        // Remove whitespace
        env_file.retain(|c| !c.is_whitespace());
        env_file_out.retain(|c| !c.is_whitespace());

        assert_eq!(env_file, env_file_out);

        Ok(())
    }
}
