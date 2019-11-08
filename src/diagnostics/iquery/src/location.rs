// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{bail, Error},
    fidl,
    fidl_fuchsia_inspect_deprecated::InspectMarker,
    files_async, io_util,
    std::{convert::TryFrom, path::PathBuf, str::FromStr},
};

/// Gets an iterator over all inspect files in a directory.
pub async fn all_locations(root: &str) -> Result<Vec<InspectLocation>, Error> {
    let mut path = std::env::current_dir()?;
    path.push(root);
    let dir_proxy = io_util::open_directory_in_namespace(
        &path.to_string_lossy().to_string(),
        io_util::OPEN_RIGHT_READABLE,
    )?;
    let locations = files_async::readdir_recursive(&dir_proxy)
        .await?
        .into_iter()
        .filter_map(|entry| {
            let mut path = PathBuf::from(&root);
            path.push(&entry.name);
            if entry.name.ends_with(<InspectMarker as fidl::endpoints::ServiceMarker>::DEBUG_NAME) {
                return Some(InspectLocation {
                    inspect_type: InspectType::DeprecatedFidl,
                    path,
                    parts: vec![],
                });
            }
            if entry.name.ends_with(".inspect") {
                return Some(InspectLocation {
                    inspect_type: InspectType::Vmo,
                    path,
                    parts: vec![],
                });
            }
            None
        })
        .collect::<Vec<InspectLocation>>();
    Ok(locations)
}

/// Type of the inspect file.
#[derive(Debug, Clone, Eq, PartialEq, Ord, PartialOrd)]
pub enum InspectType {
    Vmo,
    DeprecatedFidl,
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
        // /hub/r/test/*/c/iquery_example_component.cmx/*/out/objects/root.inspect
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
        let service_name = <InspectMarker as fidl::endpoints::ServiceMarker>::DEBUG_NAME;
        Ok(self.absolute_path()?.replace(&format!("/{}", service_name), ""))
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
    type Err = failure::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let parts = s.split("#").collect::<Vec<&str>>();
        if parts.len() > 2 {
            bail!("Path contains more than one #");
        }

        let mut inspect_parts = vec![];
        if parts.len() == 2 {
            inspect_parts.extend(parts[1].split("/"));
        }

        let mut path = PathBuf::from(parts[0]);
        let mut location = InspectLocation::try_from(path.clone()).or_else(|_| {
            // Some valid locations won't include the `fuchsia.inspect.Inspect` in
            // the name and will be just the directory. Append the name and attempt
            // to load that file.
            let service_name = <InspectMarker as fidl::endpoints::ServiceMarker>::DEBUG_NAME;
            path.push(service_name);
            InspectLocation::try_from(path)
        })?;
        location.parts = inspect_parts.into_iter().map(|p| p.to_string()).collect();
        Ok(location)
    }
}

impl ToString for InspectLocation {
    fn to_string(&self) -> String {
        let service_name = <InspectMarker as fidl::endpoints::ServiceMarker>::DEBUG_NAME;
        self.path.to_string_lossy().to_string().replace(&format!("/{}", service_name), "")
    }
}

impl TryFrom<PathBuf> for InspectLocation {
    type Error = failure::Error;

    fn try_from(path: PathBuf) -> Result<Self, Self::Error> {
        match path.file_name() {
            None => bail!("Failed to get filename"),
            Some(filename) => {
                if filename == <InspectMarker as fidl::endpoints::ServiceMarker>::DEBUG_NAME {
                    Ok(InspectLocation {
                        inspect_type: InspectType::DeprecatedFidl,
                        path,
                        parts: vec![],
                    })
                } else if filename.to_string_lossy().ends_with(".inspect") {
                    Ok(InspectLocation { inspect_type: InspectType::Vmo, path, parts: vec![] })
                } else {
                    bail!("Not an inspect file")
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
            path: PathBuf::from("/hub/c/test.cmx/123/objects"),
            parts: vec!["a".to_string(), "b".to_string()],
        };
        assert_eq!(location.query_path(), vec!["root".to_string(), "a".to_string()]);
    }
}
