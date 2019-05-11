// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys,
    lazy_static::lazy_static,
    std::collections::HashMap,
    std::path::PathBuf,
};

lazy_static! {
    pub static ref PKG_PATH: PathBuf = PathBuf::from("/pkg");
}

/// clone_component_namespace will create a duplicate namespace struct, with different channels
/// (but to the same place) for the directories. It must take ownership of the namespace to be
/// cloned, because ownership is required to use the directory handles to call the FIDL clone
/// function.
pub fn clone_component_namespace(
    mut ns: fsys::ComponentNamespace,
) -> Result<(fsys::ComponentNamespace, fsys::ComponentNamespace), Error> {
    let mut old_dirs = Vec::new();
    let mut new_dirs = Vec::new();
    for dir in ns.directories.drain(..) {
        // Convert the directory into a Proxy and call Clone
        let dir_proxy = dir.into_proxy()?;
        let new_dir_proxy = io_util::clone_directory(&dir_proxy)?;

        // Convert the preexisting directory handle back into a ClientEnd and put it in old_dirs
        let dir_chan = dir_proxy.into_channel().map_err(|_| err_msg("into_channel failed"))?; // pretty sure this is impossible here
        old_dirs.push(ClientEnd::new(dir_chan.into_zx_channel()));

        // Convert the new directory handle into a ClientEnd and put it in new_dirs
        let new_dir_chan =
            new_dir_proxy.into_channel().map_err(|_| err_msg("into_channel failed"))?; // pretty sure this is impossible here
        new_dirs.push(ClientEnd::new(new_dir_chan.into_zx_channel()));
    }
    Ok((
        fsys::ComponentNamespace { paths: ns.paths.clone(), directories: old_dirs },
        fsys::ComponentNamespace { paths: ns.paths, directories: new_dirs },
    ))
}

/// clone_component_namespace_map will create a clone of the given HashMap by calling the Clone
/// function on all the DirectoryProxy's in the map
pub fn clone_component_namespace_map(
    ns_map: &HashMap<PathBuf, DirectoryProxy>,
) -> Result<HashMap<PathBuf, DirectoryProxy>, Error> {
    let mut new_map = HashMap::new();
    for (path, dir) in ns_map {
        new_map.insert(path.clone(), io_util::clone_directory(&dir)?);
    }
    Ok(new_map)
}

/// ns_to_map will convert the given namespace into a HashMap, where each path is the key for the
/// directory of the same index. If the lengths of ns.paths and ns.directories do not match, they
/// are truncated.
pub fn ns_to_map(
    mut ns: fsys::ComponentNamespace,
) -> Result<HashMap<PathBuf, DirectoryProxy>, Error> {
    // Put the directories in a HashMap for easy lookup
    let mut ns_map = HashMap::new();
    while let Some(path) = ns.paths.pop() {
        if let Some(dir) = ns.directories.pop() {
            let dir_proxy = dir.into_proxy()?;
            ns_map.insert(PathBuf::from(path), dir_proxy);
        }
    }
    Ok(ns_map)
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[test]
    fn clone_ns_test() {
        let mut executor = fasync::Executor::new().unwrap();
        executor.run_singlethreaded(async {
            // Get a handle to /bin
            let bin_path = "/bin".to_string();
            let bin_proxy = io_util::open_directory_in_namespace("/pkg/bin").unwrap();
            let bin_chan = bin_proxy.into_channel().unwrap();
            let bin_handle = ClientEnd::new(bin_chan.into_zx_channel());

            // Get a handle to /lib
            let lib_path = "/lib".to_string();
            let lib_proxy = io_util::open_directory_in_namespace("/pkg/lib").unwrap();
            let lib_chan = lib_proxy.into_channel().unwrap();
            let lib_handle = ClientEnd::new(lib_chan.into_zx_channel());

            let ns = fsys::ComponentNamespace {
                paths: vec![lib_path, bin_path],
                directories: vec![lib_handle, bin_handle],
            };

            // Load in a VMO holding the target executable from the namespace
            let (mut _ns, ns_clone) = clone_component_namespace(ns).unwrap();
            let ns_map = ns_to_map(ns_clone).unwrap();

            let dir = ns_map.get(&PathBuf::from("/lib")).unwrap();
            let path = PathBuf::from("ld.so.1");
            io_util::open_file(&dir, &path).unwrap();
            let dir = ns_map.get(&PathBuf::from("/bin")).unwrap();
            let path = PathBuf::from("hello_world");
            io_util::open_file(&dir, &path).unwrap();
        });
    }
}
