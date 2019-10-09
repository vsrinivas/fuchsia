// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::location::{InspectLocation, InspectType},
    failure::{format_err, Error},
    fidl_fuchsia_io::NodeInfo,
    fuchsia_inspect::reader::NodeHierarchy,
    inspect_fidl_load as inspect_fidl, io_util,
    std::convert::TryFrom,
};

#[derive(Debug)]
pub struct IqueryResult {
    pub location: InspectLocation,
    pub hierarchy: NodeHierarchy,
}

impl IqueryResult {
    pub async fn try_from(location: InspectLocation) -> Result<Self, Error> {
        match location.inspect_type {
            InspectType::Vmo => Self::try_from_vmo(location).await,
            InspectType::DeprecatedFidl => Self::try_from_fidl(location).await,
        }
    }

    async fn try_from_vmo(location: InspectLocation) -> Result<Self, Error> {
        let proxy = io_util::open_file_in_namespace(
            &location.absolute_path()?,
            io_util::OPEN_RIGHT_READABLE,
        )?;

        // Obtain the vmo backing any VmoFiles.
        let node_info = proxy.describe().await?;
        match node_info {
            NodeInfo::Vmofile(vmofile) => NodeHierarchy::try_from(&vmofile.vmo),
            NodeInfo::File(_) => {
                let bytes = io_util::read_file_bytes(&proxy).await?;
                NodeHierarchy::try_from(bytes)
            }
            _ => Err(format_err!("Unknown inspect file at {}", location.path.display())),
        }
        .map(|hierarchy| Self { location, hierarchy })
    }

    async fn try_from_fidl(location: InspectLocation) -> Result<Self, Error> {
        let path = location.absolute_path()?;
        let hierarchy = inspect_fidl::load_hierarchy_from_path(&path).await?;
        Ok(IqueryResult { location, hierarchy })
    }

    pub fn get_query_hierarchy(&self) -> Option<&NodeHierarchy> {
        if self.location.parts.is_empty() {
            return Some(&self.hierarchy);
        }
        let remaining_path = &self.location.parts[..];
        self.hierarchy.children.iter().filter_map(move |c| self.query(c, remaining_path)).next()
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
        assert_eq!(
            result.get_query_hierarchy(),
            Some(&NodeHierarchy { name: "b".to_string(), children: vec![], properties: vec![] })
        );
        let result = test_result(vec!["c".to_string(), "b".to_string()]);
        assert_eq!(result.get_query_hierarchy(), None);
    }

    fn test_result(parts: Vec<String>) -> IqueryResult {
        IqueryResult {
            location: InspectLocation {
                inspect_type: InspectType::Vmo,
                path: PathBuf::from("/hub/c/test.cmx/123/objects"),
                parts,
            },
            hierarchy: NodeHierarchy {
                name: "root".to_string(),
                properties: vec![],
                children: vec![NodeHierarchy {
                    name: "a".to_string(),
                    properties: vec![],
                    children: vec![NodeHierarchy {
                        name: "b".to_string(),
                        children: vec![],
                        properties: vec![],
                    }],
                }],
            },
        }
    }
}
