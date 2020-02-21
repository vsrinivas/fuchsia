// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::configuration::ConfigLevel,
    anyhow::{anyhow, Error},
    serde_derive::{Deserialize, Serialize},
    serde_json::{Map, Value},
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
    pub defaults: Option<String>,
}

impl Environment {
    fn load_from_reader<R: Read>(reader: R) -> Result<Self, Error> {
        match serde_json::from_reader::<R, Environment>(reader) {
            Ok(value) => Ok(value),
            Err(e) => Err(anyhow!("Could not initialize configuration environment {}", e)),
        }
    }

    fn save_to_writer<W: Write>(&self, writer: W) -> Result<(), Error> {
        match serde_json::to_writer_pretty(writer, &self) {
            Err(e) => Err(anyhow!("Could not write config environment file: {}", e)),
            Ok(_) => Ok(()),
        }
    }

    pub fn load(file: &str) -> Result<Self, Error> {
        Environment::load_from_reader(Environment::reader(file)?)
    }

    fn reader(path: &str) -> Result<BufReader<File>, Error> {
        match File::open(path) {
            Ok(f) => Ok(BufReader::new(f)),
            Err(e) => Err(anyhow!("Could not open file {}", e)),
        }
    }

    fn writer(path: &str) -> Result<BufWriter<File>, Error> {
        let file = OpenOptions::new().write(true).create(true).open(path);
        match file {
            Ok(f) => Ok(BufWriter::new(f)),
            Err(e) => Err(anyhow!("Could not open file {}", e)),
        }
    }

    pub fn save(&self, file: &str) -> Result<(), Error> {
        self.save_to_writer(Environment::writer(file)?)
    }

    fn display_user(&self) -> String {
        match self.user.as_ref() {
            Some(u) => format!(" User: {}\n", u),
            None => format!(" User: none\n"),
        }
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
        match self.global.as_ref() {
            Some(g) => format!(" Global: {}\n", g),
            None => format!(" Global: none\n"),
        }
    }

    fn display_defaults(&self) -> String {
        match self.defaults.as_ref() {
            Some(d) => format!(" Defaults: {}\n", d),
            None => format!(" Defaults: none\n"),
        }
    }

    pub fn display(&self, level: &Option<ConfigLevel>) -> String {
        match level {
            Some(l) => match l {
                ConfigLevel::User => self.display_user(),
                ConfigLevel::Build => self.display_build(),
                ConfigLevel::Global => self.display_global(),
                ConfigLevel::Defaults => self.display_defaults(),
            },
            None => {
                let mut res = format!("\nEnvironment:\n");
                res.push_str(&self.display_user());
                res.push_str(&self.display_build());
                res.push_str(&self.display_global());
                res.push_str(&self.display_defaults());
                res
            }
        }
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
            "global": "/tmp/global.json",
            "defaults": "/tmp/defaults.json"
        }"#;

    #[test]
    fn test_loading_and_saving_environment() -> Result<(), Error> {
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
