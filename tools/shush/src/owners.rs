// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use lazy_static::lazy_static;
use regex::Regex;

use std::{
    fmt,
    fs::File,
    io::{self, BufRead, BufReader},
    path::{Path, PathBuf},
};

#[derive(Clone, Debug)]
pub struct Owners {
    #[allow(unused)]
    pub path: PathBuf,
    pub users: Vec<String>,
    pub component: Option<String>,
}

impl Owners {
    /// Reads an OWNERS file and parses out the usernames and component name.
    /// NOTE: this ignores per-file components/owners for simplicity, as
    /// well as multiple-components (which are quite rare). "include" syntax
    /// currently isn't supported either, but may be handled in the future.
    pub fn from_file(filename: &Path) -> io::Result<Self> {
        lazy_static! {
            static ref USER: Regex = Regex::new(r"(\w+)@google.com").unwrap();
            static ref COMP: Regex = Regex::new(r"COMPONENT: *(\S+)").unwrap();
        }
        let mut users = Vec::new();
        let mut component = None;
        for line in BufReader::new(File::open(&filename)?).lines() {
            let line = line?;
            if let Some(cap) = USER.captures(&line) {
                users.push(cap.get(1).unwrap().as_str().to_owned());
            } else if let Some(cap) = COMP.captures(&line) {
                component = Some(cap.get(1).unwrap().as_str().to_owned());
            }
        }
        Ok(Self { path: filename.to_path_buf(), users, component })
    }
}

/// Walks up the directory tree and gathers any OWNERS files found. The
/// returned list starts with the _closest_ OWNERS file to the given path, and
/// it does NOT include OWNERS files in the root dir.
pub fn get_owners(path: &Path, root: &Path) -> Vec<Owners> {
    let mut prev: &Path = &root.join(path);
    let mut all_owners = Vec::new();
    while let Some(current) = prev.parent() {
        if current == root {
            break;
        }
        let ownersfile = current.join("OWNERS");
        if ownersfile.exists() {
            if let Ok(owners) = Owners::from_file(&ownersfile) {
                all_owners.push(owners);
            }
        }
        prev = current;
    }
    all_owners
}

#[derive(Debug, Eq, Hash, PartialEq)]
pub struct FileOwnership {
    pub component: Option<String>,
    pub owners: Vec<String>,
}

impl FileOwnership {
    pub fn from_path(path: &Path, fuchsia_dir: &Path) -> Self {
        let owners = get_owners(path, fuchsia_dir);

        let component = owners.iter().find_map(|o| o.component.clone());
        let owners = owners
            .into_iter()
            .find(|o| !o.users.is_empty())
            .map(|o| o.users)
            .unwrap_or_else(|| Vec::new());

        Self { component, owners }
    }
}

impl fmt::Display for FileOwnership {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let owners =
            if !self.owners.is_empty() { self.owners.join(",") } else { "Unowned".to_string() };

        if let Some(ref component) = self.component {
            write!(f, "{} ({})", component, owners)
        } else {
            write!(f, "{}", owners)
        }
    }
}
