// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::location::{InspectLocation, InspectType},
    failure::{format_err, Error},
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_io::NodeInfo,
    fuchsia_inspect::reader::{NodeHierarchy, PartialNodeHierarchy},
    inspect_fidl_load as inspect_fidl, io_util,
    std::convert::TryFrom,
};

#[derive(Debug)]
pub struct IqueryResult {
    pub location: InspectLocation,
    pub hierarchy: Option<NodeHierarchy>,
}

impl IqueryResult {
    pub fn new(location: InspectLocation) -> Self {
        Self { location, hierarchy: None }
    }

    pub async fn try_from(location: InspectLocation) -> Result<Self, Error> {
        let mut result = Self::new(location);
        result.load().await?;
        Ok(result)
    }

    pub async fn load(&mut self) -> Result<(), Error> {
        match self.location.inspect_type {
            InspectType::Vmo => self.load_from_vmo().await,
            InspectType::Tree => self.load_from_tree().await,
            InspectType::DeprecatedFidl => self.load_from_fidl().await,
        }
    }

    pub fn is_loaded(&self) -> bool {
        self.hierarchy.is_some()
    }

    pub fn sort_hierarchy(&mut self) {
        match self.hierarchy {
            None => return,
            Some(ref mut hierarchy) => hierarchy.sort(),
        }
    }

    async fn load_from_tree(&mut self) -> Result<(), Error> {
        let path = self.location.absolute_path()?;
        let (tree, server) = fidl::endpoints::create_proxy::<TreeMarker>()?;
        fdio::service_connect(&path, server.into_channel())?;
        self.hierarchy = Some(NodeHierarchy::try_from_tree(&tree).await?);
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

    pub fn get_query_hierarchy(&self) -> Option<&NodeHierarchy> {
        if self.location.parts.is_empty() {
            return self.hierarchy.as_ref();
        }
        let remaining_path = &self.location.parts[..];
        self.hierarchy.as_ref().and_then(|hierarchy| {
            hierarchy.children.iter().filter_map(move |c| self.query(c, remaining_path)).next()
        })
    }

    fn query<'a>(
        &'a self,
        hierarchy: &'a NodeHierarchy,
        path: &'a [String],
    ) -> Option<&'a NodeHierarchy> {
        if path.is_empty() || hierarchy.name != path[0] {
            return None;
        }
        if path.len() == 1 && hierarchy.name == path[0] {
            return Some(hierarchy);
        }
        hierarchy.children.iter().filter_map(|c| self.query(c, &path[1..])).next()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::path::PathBuf};

    #[test]
    fn get_query_hierarchy() {
        let result = test_result(vec!["a".to_string(), "b".to_string()]);
        assert_eq!(result.get_query_hierarchy(), Some(&NodeHierarchy::new("b", vec![], vec![])));
        let result = test_result(vec!["c".to_string(), "b".to_string()]);
        assert_eq!(result.get_query_hierarchy(), None);
    }

    fn test_result(parts: Vec<String>) -> IqueryResult {
        IqueryResult {
            location: InspectLocation {
                inspect_type: InspectType::Vmo,
                path: PathBuf::from("/hub/c/test.cmx/123/out/diagnostics"),
                parts,
            },
            hierarchy: Some(NodeHierarchy::new(
                "root",
                vec![],
                vec![NodeHierarchy::new(
                    "a",
                    vec![],
                    vec![NodeHierarchy::new("b", vec![], vec![])],
                )],
            )),
        }
    }
}
