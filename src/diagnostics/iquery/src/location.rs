// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_inspect_deprecated::InspectMarker,
    std::{
        collections::VecDeque, convert::TryFrom, fs, iter::FromIterator, path::PathBuf,
        str::FromStr,
    },
};

/// Gets an iterator over all inspect files in a directory.
pub fn all_locations(root: &str) -> Box<dyn Iterator<Item = InspectLocation>> {
    match InspectLocationIterator::try_from(root) {
        Ok(iterator) => Box::new(iterator),
        Err(e) => {
            eprintln!("Error while reading dir {}: {}", root, e);
            Box::new(std::iter::empty::<InspectLocation>())
        }
    }
}

/// Type of the inspect file.
#[derive(Debug, Clone, Eq, PartialEq, Ord, PartialOrd)]
pub enum InspectType {
    Vmo,
    DeprecatedFidl,
    Tree,
}

/// InspectLocation of an inspect file.
#[derive(Debug, Clone, Eq, PartialEq, Ord, PartialOrd)]
pub struct InspectLocation {
    /// The type of the inspect location.
    pub inspect_type: InspectType,

    /// The path to the inspect location.
    pub path: PathBuf,

    /// The parts of the inspect object this location cares about.
    /// If empty, it means all.
    pub parts: Vec<String>,
}

impl InspectLocation {
    pub fn absolute_path(&self) -> Result<String, Error> {
        // Note: self.path.canonicalize() returns error for files such as:
        // /hub/r/test/*/c/iquery_example_component.cmx/*/out/diagnostics/root.inspect
        // Hence, getting the absolute path manually.
        let current_dir = std::env::current_dir()?.to_string_lossy().to_string();
        let path_string =
            self.path.canonicalize().unwrap_or(self.path.clone()).to_string_lossy().to_string();
        if path_string.is_empty() {
            return Ok(current_dir);
        }
        if path_string.chars().next() == Some('/') {
            return Ok(path_string);
        }
        if current_dir == "/" {
            return Ok(format!("/{}", path_string));
        }
        Ok(format!("{}/{}", current_dir, path_string))
    }

    pub fn absolute_path_to_string(&self) -> Result<String, Error> {
        Ok(strip_service_suffix(self.absolute_path()?))
    }

    pub fn query_path(&self) -> Vec<String> {
        let mut path = vec![];
        if !self.parts.is_empty() {
            path = self.parts.clone();
            path.pop(); // Remove the last one given that |hierarchy.name| is that one.
            path.insert(0, "root".to_string()); // Parts won't contain "root"
        }
        path
    }
}

impl FromStr for InspectLocation {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let parts = s.split("#").collect::<Vec<&str>>();
        if parts.len() > 2 {
            return Err(format_err!("Path contains more than one #"));
        }

        let mut inspect_parts = vec![];
        if parts.len() == 2 {
            inspect_parts.extend(parts[1].split("/"));
        }

        // Some valid locations won't include the service name in the name and will be just the
        // directory. Append the name and attempt to load that file.
        let mut location = InspectLocation::try_from(PathBuf::from(parts[0]))
            .or_else(|_| {
                let mut path = PathBuf::from(parts[0]);
                path.push(InspectMarker::SERVICE_NAME);
                InspectLocation::try_from(path)
            })
            .or_else(|_| {
                let mut path = PathBuf::from(parts[0]);
                path.push(TreeMarker::SERVICE_NAME);
                InspectLocation::try_from(path)
            })?;
        location.parts = inspect_parts.into_iter().map(|p| p.to_string()).collect();
        Ok(location)
    }
}

fn strip_service_suffix(string: String) -> String {
    string
        .replace(&format!("/{}", InspectMarker::SERVICE_NAME), "")
        .replace(&format!("/{}", TreeMarker::SERVICE_NAME), "")
}

impl ToString for InspectLocation {
    fn to_string(&self) -> String {
        strip_service_suffix(self.path.to_string_lossy().to_string())
    }
}

impl TryFrom<PathBuf> for InspectLocation {
    type Error = anyhow::Error;

    fn try_from(path: PathBuf) -> Result<Self, Self::Error> {
        match path.file_name() {
            None => return Err(format_err!("Failed to get filename")),
            Some(filename) => {
                if filename == InspectMarker::SERVICE_NAME && path.exists() {
                    Ok(InspectLocation {
                        inspect_type: InspectType::DeprecatedFidl,
                        path,
                        parts: vec![],
                    })
                } else if filename == TreeMarker::SERVICE_NAME && path.exists() {
                    Ok(InspectLocation { inspect_type: InspectType::Tree, path, parts: vec![] })
                } else if filename.to_string_lossy().ends_with(".inspect") {
                    Ok(InspectLocation { inspect_type: InspectType::Vmo, path, parts: vec![] })
                } else {
                    return Err(format_err!("Not an inspect file"));
                }
            }
        }
    }
}

/// Iterates over a directory tree and returns all Inspect files.
struct InspectLocationIterator {
    pending: VecDeque<fs::DirEntry>,
}

impl TryFrom<&str> for InspectLocationIterator {
    type Error = std::io::Error;

    fn try_from(root: &str) -> Result<Self, Self::Error> {
        let pending = VecDeque::from_iter(fs::read_dir(root)?.filter_map(|e| e.ok()));
        Ok(Self { pending })
    }
}

impl Iterator for InspectLocationIterator {
    type Item = InspectLocation;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if self.pending.is_empty() {
                return None;
            }
            let path = self.pending.pop_front()?.path();
            if path.is_dir() {
                match fs::read_dir(path) {
                    Ok(entries) => self.pending.extend(entries.filter_map(|e| e.ok())),
                    _ => {}
                }
            } else {
                match InspectLocation::try_from(path) {
                    Ok(location) => return Some(location),
                    Err(_) => continue,
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn query_path() {
        let location = InspectLocation {
            inspect_type: InspectType::Vmo,
            path: PathBuf::from("/hub/c/test.cmx/123/out/diagnostics"),
            parts: vec!["a".to_string(), "b".to_string()],
        };
        assert_eq!(location.query_path(), vec!["root".to_string(), "a".to_string()]);
    }
}
