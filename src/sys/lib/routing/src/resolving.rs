// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fidl_fuchsia_io as fio,
    fuchsia_url::{
        errors::ParseError as PkgUrlParseError, errors::ResourcePathError, pkg_url::PkgUrl,
    },
    lazy_static::lazy_static,
    std::collections::HashMap,
    std::path::Path,
};

lazy_static! {
    static ref SUBPACKAGES_PATH: &'static Path = Path::new("meta/subpackages");
}

pub fn context_bytes_from_subpackages_map(
    some_subpackage_hashes: Option<&HashMap<String, String>>,
) -> anyhow::Result<Option<Vec<u8>>> {
    let subpackage_hashes = if let Some(subpackage_hashes) = some_subpackage_hashes {
        subpackage_hashes
    } else {
        return Ok(None);
    };
    if subpackage_hashes.is_empty() {
        Ok(None)
    } else {
        let subpackages_json_str = serde_json::to_string(&subpackage_hashes)?;
        Ok(Some(subpackages_json_str.into_bytes()))
    }
}

pub fn subpackages_map_from_context_bytes(
    context_bytes: &Vec<u8>,
) -> anyhow::Result<HashMap<String, String>> {
    let subpackages_json = String::from_utf8(context_bytes.clone())?;
    let json_value: serde_json::Value = serde_json::from_str(&subpackages_json)?;
    Ok(serde_json::from_value::<HashMap<String, String>>(json_value)?)
}

/// Given a component URL (which may be absolute, or may start with a relative
/// package path), extract the component resource (from the URL fragment) and
/// create the `PkgUrl`, and return both, as a tuple.
pub fn parse_package_url_and_resource(
    component_url: &str,
) -> Result<(PkgUrl, String), PkgUrlParseError> {
    let pkg_url = PkgUrl::parse_maybe_relative(component_url)?;
    let resource = pkg_url
        .resource()
        .ok_or_else(|| PkgUrlParseError::InvalidResourcePath(ResourcePathError::PathIsEmpty))?;
    Ok((pkg_url.root_url(), resource.to_string()))
}

pub async fn read_subpackages(
    dir: &fio::DirectoryProxy,
) -> anyhow::Result<Option<HashMap<String, String>>> {
    if let Ok(subpackages_file) =
        io_util::open_file(&dir, &SUBPACKAGES_PATH, fio::OpenFlags::RIGHT_READABLE)
    {
        match io_util::read_file(&subpackages_file).await {
            Ok(subpackages) => Ok(Some(convert_subpackages_file_content_to_hashmap(&subpackages)?)),
            Err(e) => match e.downcast_ref::<io_util::file::ReadError>() {
                Some(_) => Ok(None), // Indicates the file does not exist.
                None => Err(anyhow!(e)),
            },
        }
    } else {
        Ok(None)
    }
}

fn convert_subpackages_file_content_to_hashmap(
    subpackages: &str,
) -> anyhow::Result<HashMap<String, String>> {
    let mut subpackages_map = HashMap::new();
    for line in subpackages.lines() {
        let mut s = line.split('=');
        if let Some(subpackage_name) = s.next() {
            let hash = s.next().ok_or_else(|| {
                anyhow::format_err!("subpackage '{}' is missing its hash", subpackage_name)
            })?;
            subpackages_map.insert(subpackage_name.to_string(), hash.into());
        }
    }
    Ok(subpackages_map)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn test_parse_package_url_and_resource() -> anyhow::Result<()> {
        let (abs_pkgurl, resource) =
            parse_package_url_and_resource("fuchsia-pkg://fuchsia.com/package#meta/comp.cm")?;
        assert_eq!(abs_pkgurl.is_relative(), false);
        assert_eq!(abs_pkgurl.host(), "fuchsia.com");
        assert_eq!(abs_pkgurl.name().as_ref(), "package");
        assert_eq!(resource, "meta/comp.cm");

        let (rel_pkgurl, resource) = parse_package_url_and_resource("package#meta/comp.cm")?;
        assert_eq!(rel_pkgurl.is_relative(), true);
        assert_eq!(rel_pkgurl.name().as_ref(), "package");
        assert_eq!(resource, "meta/comp.cm");
        Ok(())
    }

    #[cfg(target_os = "fuchsia")]
    mod fuchsia_tests {
        use {
            super::*,
            fidl::endpoints::create_proxy,
            std::sync::Arc,
            vfs::{
                self, directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
                file::vmo::asynchronous::read_only_const,
            },
        };

        fn serve_vfs_dir(root: Arc<impl DirectoryEntry>) -> fio::DirectoryProxy {
            let fs_scope = ExecutionScope::new();
            let (client, server) = create_proxy::<fio::DirectoryMarker>().unwrap();
            root.open(
                fs_scope.clone(),
                fio::OpenFlags::RIGHT_READABLE,
                0,
                vfs::path::Path::dot(),
                fidl::endpoints::ServerEnd::new(server.into_channel()),
            );
            client
        }

        #[fuchsia::test]
        async fn test_read_subpackages() -> anyhow::Result<()> {
            let subpackage_name = "some_subpackage";
            let subpackage_hash =
                "facefacefacefacefacefacefacefacefacefacefacefacefacefacefaceface";
            let dir_proxy = serve_vfs_dir(vfs::pseudo_directory! {
                "meta" => vfs::pseudo_directory! {
                    "subpackages" => read_only_const(&format!("{}={}\n", subpackage_name, subpackage_hash).into_bytes()),
                }
            });
            let subpackage_hashes = read_subpackages(&dir_proxy).await?.unwrap();
            assert_eq!(subpackage_hashes.get(subpackage_name), Some(&subpackage_hash.to_string()));
            Ok(())
        }
    }
}
