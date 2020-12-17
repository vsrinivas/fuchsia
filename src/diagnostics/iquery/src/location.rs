// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_inspect_deprecated::InspectMarker,
    fidl_fuchsia_io::NodeInfo,
    files_async,
    fuchsia_inspect::reader::{self, DiagnosticsHierarchy, PartialNodeHierarchy},
    fuchsia_zircon::DurationNum,
    futures::stream::StreamExt,
    inspect_fidl_load as inspect_fidl, io_util,
    lazy_static::lazy_static,
    std::{convert::TryFrom, path::PathBuf, str::FromStr},
};

lazy_static! {
    static ref EXPECTED_FILES: Vec<(String, InspectType)> = vec![
        (<TreeMarker as fidl::endpoints::ServiceMarker>::DEBUG_NAME.to_string(), InspectType::Tree),
        (
            <InspectMarker as fidl::endpoints::ServiceMarker>::DEBUG_NAME.to_string(),
            InspectType::DeprecatedFidl,
        ),
        (".inspect".to_string(), InspectType::Vmo),
    ];
    static ref READDIR_TIMEOUT_SECONDS: i64 = 15;
}

/// Gets an iterator over all inspect files in a directory.
pub async fn all_locations(root: impl AsRef<str>) -> Result<Vec<InspectLocation>, Error> {
    let mut path = std::env::current_dir()?;
    let root = root.as_ref();
    path.push(&root);
    let dir_proxy = io_util::open_directory_in_namespace(
        &path.to_string_lossy().to_string(),
        io_util::OPEN_RIGHT_READABLE,
    )?;

    let locations =
        files_async::readdir_recursive(&dir_proxy, Some(READDIR_TIMEOUT_SECONDS.seconds()))
            .filter_map(|result| async move {
                match result {
                    Err(err) => {
                        eprintln!("{}", err);
                        None
                    }
                    Ok(entry) => {
                        let mut path = PathBuf::from(&root);
                        path.push(&entry.name);
                        EXPECTED_FILES
                            .iter()
                            .find(|(filename, _)| entry.name.ends_with(filename))
                            .map(|(_, inspect_type)| InspectLocation {
                                inspect_type: inspect_type.clone(),
                                path,
                            })
                    }
                }
            })
            .collect::<Vec<InspectLocation>>()
            .await;
    Ok(locations)
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
}

impl InspectLocation {
    pub fn absolute_path(&self) -> Result<String, Error> {
        // Note: self.path.canonicalize() returns error for files such as:
        // /hub/r/test/*/c/iquery_test_component.cmx/*/out/diagnostics/root.inspect
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

    pub async fn load(self) -> Result<InspectObject, Error> {
        InspectObject::new(self).await
    }
}

impl FromStr for InspectLocation {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        // Some valid locations won't include the service name in the name and will be just the
        // directory. Append the name and attempt to load that file.
        InspectLocation::try_from(PathBuf::from(s))
            .or_else(|_| {
                let mut path = PathBuf::from(s);
                path.push(InspectMarker::SERVICE_NAME);
                InspectLocation::try_from(path)
            })
            .or_else(|_| {
                let mut path = PathBuf::from(s);
                path.push(TreeMarker::SERVICE_NAME);
                InspectLocation::try_from(path)
            })
    }
}

impl TryFrom<PathBuf> for InspectLocation {
    type Error = anyhow::Error;

    fn try_from(path: PathBuf) -> Result<Self, Self::Error> {
        match path.file_name() {
            None => return Err(format_err!("Failed to get filename")),
            Some(filename) => {
                if filename == InspectMarker::SERVICE_NAME && path.exists() {
                    Ok(InspectLocation { inspect_type: InspectType::DeprecatedFidl, path })
                } else if filename == TreeMarker::SERVICE_NAME && path.exists() {
                    Ok(InspectLocation { inspect_type: InspectType::Tree, path })
                } else if filename.to_string_lossy().ends_with(".inspect") {
                    Ok(InspectLocation { inspect_type: InspectType::Vmo, path })
                } else {
                    return Err(format_err!("Not an inspect file"));
                }
            }
        }
    }
}

#[derive(Debug)]
pub struct InspectObject {
    pub location: InspectLocation,
    pub hierarchy: Option<DiagnosticsHierarchy>,
}

impl InspectObject {
    async fn new(location: InspectLocation) -> Result<Self, Error> {
        let mut this = Self { location, hierarchy: None };
        match this.location.inspect_type {
            InspectType::Vmo => this.load_from_vmo().await?,
            InspectType::Tree => this.load_from_tree().await?,
            InspectType::DeprecatedFidl => this.load_from_fidl().await?,
        }
        Ok(this)
    }

    async fn load_from_tree(&mut self) -> Result<(), Error> {
        let path = self.location.absolute_path()?;
        let (tree, server) = fidl::endpoints::create_proxy::<TreeMarker>()?;
        fdio::service_connect(&path, server.into_channel())?;
        self.hierarchy = Some(reader::read(&tree).await?);
        Ok(())
    }

    async fn load_from_vmo(&mut self) -> Result<(), Error> {
        let proxy = io_util::open_file_in_namespace(
            &self.location.absolute_path()?,
            io_util::OPEN_RIGHT_READABLE,
        )?;

        // Obtain the vmo backing any VmoFiles.
        let node_info = proxy.describe().await?;
        match node_info {
            NodeInfo::Vmofile(vmofile) => {
                self.hierarchy = Some(PartialNodeHierarchy::try_from(&vmofile.vmo)?.into());
                Ok(())
            }
            NodeInfo::File(_) => {
                let bytes = io_util::read_file_bytes(&proxy).await?;
                self.hierarchy = Some(PartialNodeHierarchy::try_from(bytes)?.into());
                Ok(())
            }
            _ => Err(format_err!("Unknown inspect file at {}", self.location.path.display())),
        }
    }

    async fn load_from_fidl(&mut self) -> Result<(), Error> {
        let path = self.location.absolute_path()?;
        self.hierarchy = Some(inspect_fidl::load_hierarchy_from_path(&path).await?);
        Ok(())
    }
}
