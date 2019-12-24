// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::{create_proxy, ServerEnd};
use fidl_fuchsia_io::{DirectoryProxy, FileMarker, NodeMarker};
use fidl_fuchsia_sys::{FlatNamespace, RunnerRequest, RunnerRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::fx_log_info;
use fuchsia_url::pkg_url::PkgUrl;
use fuchsia_zircon as zx;
use futures::prelude::*;

use std::mem;

fn manifest_path_from_url(url: &str) -> Result<String, Error> {
    match PkgUrl::parse(url) {
        Ok(url) => match url.resource() {
            Some(r) => Ok(r.to_string()),
            None => return Err(format_err!("no resource")),
        },
        Err(e) => Err(e),
    }
    .map_err(|e| format_err!("parse error {}", e))
}

fn extract_directory_with_name(ns: &mut FlatNamespace, name: &str) -> Result<zx::Channel, Error> {
    let handle_ref = ns
        .paths
        .iter()
        .zip(ns.directories.iter_mut())
        .find(|(n, _)| n.as_str() == name)
        .ok_or_else(|| format_err!("could not find entry matching {}", name))
        .map(|x| x.1)?;
    Ok(mem::replace(handle_ref, zx::Channel::from(zx::Handle::invalid())))
}

async fn file_contents_at_path(dir: zx::Channel, path: &str) -> Result<Vec<u8>, Error> {
    let dir_proxy = DirectoryProxy::new(fasync::Channel::from_channel(dir)?);

    let (file, server) = create_proxy::<FileMarker>()?;

    dir_proxy.open(0, 0, path, ServerEnd::<NodeMarker>::new(server.into_channel()))?;

    let attr = file.get_attr().await?.1;

    let (_, vec) = file.read(attr.content_size).await?;
    Ok(vec)
}

#[derive(Default)]
#[allow(unused)]
struct TestFacet {
    component_under_test: String,
    injected_services: Vec<String>,
    system_services: Vec<String>,
}

// TODO(jamesr): Use serde to validate and deserialize the facet directly.
// See //garnet/bin/cmc/src/validate.rs for reference.
fn test_facet(meta: &serde_json::Value) -> Result<TestFacet, Error> {
    let facets = meta.get("facets").ok_or_else(|| format_err!("no facets"))?;
    if !facets.is_object() {
        return Err(format_err!("facet not an object"));
    }
    let fuchsia_test_facet = match facets.get("fuchsia.test") {
        Some(v) => v,
        None => return Err(format_err!("no fuchsia.test facet")),
    };
    if !fuchsia_test_facet.is_object() {
        return Err(format_err!("fuchsia.test facet not an object"));
    }
    let component_under_test = match fuchsia_test_facet.get("component_under_test") {
        Some(v) => v,
        None => {
            return Err(format_err!("no component_under_test definition in fuchsia.test facet"))
        }
    };
    let component_under_test = component_under_test
        .as_str()
        .ok_or_else(|| format_err!("component_under_test in fuchsia.test facet not a string"))?
        .to_string();
    Ok(TestFacet {
        component_under_test: component_under_test,
        injected_services: Vec::new(),
        system_services: Vec::new(),
    })
}

async fn run_runner_server(mut stream: RunnerRequestStream) -> Result<(), Error> {
    while let Some(RunnerRequest::StartComponent {
        package,
        mut startup_info,
        controller: _,
        control_handle,
    }) = stream.try_next().await.context("error running server")?
    {
        fx_log_info!("Received runner request for component {}", package.resolved_url);

        let manifest_path = manifest_path_from_url(&package.resolved_url)?;

        fx_log_info!("Component manifest path {}", manifest_path);

        let pkg_directory_channel =
            extract_directory_with_name(&mut startup_info.flat_namespace, "/pkg")?;

        fx_log_info!("Found package directory handle");

        let meta_contents = file_contents_at_path(pkg_directory_channel, &manifest_path).await?;

        fx_log_info!("Meta contents: {:#?}", std::str::from_utf8(&meta_contents)?);

        let meta = serde_json::from_slice::<serde_json::Value>(&meta_contents)?;

        fx_log_info!("Found metadata: {:#?}", meta);

        let _f = test_facet(&meta)?;

        //TODO(jamesr): Configure realm based on |f| then instantiate and watch
        //|f.component_under_test| within that realm.

        control_handle.shutdown();
    }
    Ok(())
}

enum IncomingServices {
    Runner(RunnerRequestStream),
    // ... more services here
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["component_test_runner"])?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingServices::Runner);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |IncomingServices::Runner(stream)| {
        run_runner_server(stream).unwrap_or_else(|e| println!("{:?}", e))
    });

    fut.await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon::HandleBased;
    use serde_json::json;

    /// Makes a manifest_path_from_url test
    /// Arguments:
    ///     name: name of the test case
    ///     url: url to parse
    ///     path: expected path component
    ///     err: true if an error is expected
    macro_rules! manifest_path_from_url_test {
        ( $name:ident, $url:literal, $path:literal, $err:literal ) => {
            #[test]
            fn $name() {
                match manifest_path_from_url($url) {
                    Ok(path) => {
                        assert!(!$err);
                        assert_eq!(path, $path)
                    }
                    Err(_) => assert!($err),
                }
            }
        };
    }

    manifest_path_from_url_test!(empty_string, "", "", true);
    manifest_path_from_url_test!(no_hash, "fuchsia-pkg://foo/abcdef", "", true);
    manifest_path_from_url_test!(one_hash, "fuchsia-pkg://foo/abc#def", "def", false);
    manifest_path_from_url_test!(multiple_hash, "fuchsia-pkg://foo/abc#def#ghi", "def#ghi", false);
    manifest_path_from_url_test!(last_position_hash, "fuchsia-pkg://foo/abc#", "", true);

    #[test]
    fn directory_with_name_tests() -> Result<(), Error> {
        let (a_ch_0, _) = zx::Channel::create()?;
        let (b_ch_0, _) = zx::Channel::create()?;
        let mut ns = FlatNamespace {
            paths: vec![String::from("/a"), String::from("/b")],
            directories: vec![a_ch_0, b_ch_0],
        };

        assert!(extract_directory_with_name(&mut ns, "/c/").is_err());

        assert!(extract_directory_with_name(&mut ns, "/b").is_ok());
        assert!(ns.directories[1].is_invalid_handle());

        Ok(())
    }

    #[test]
    fn test_facet_missing_facet() -> Result<(), Error> {
        let meta = json!({});
        let f = test_facet(&meta);
        assert!(f.is_err());
        Ok(())
    }

    #[test]
    fn test_facet_missing_fuchsia_test_facet() -> Result<(), Error> {
        let meta = json!({
          "facets": []
        });
        let f = test_facet(&meta);
        assert!(f.is_err());
        Ok(())
    }

    #[test]
    fn test_facet_facets_wrong_type() -> Result<(), Error> {
        let meta = json!({
          "facets": []
        });
        let f = test_facet(&meta);
        assert!(f.is_err());
        Ok(())
    }

    #[test]
    fn test_facet_fuchsia_test_facet_wrong_type() -> Result<(), Error> {
        let meta = json!({
          "facets": {
            "fuchsia.test": []
          }
        });
        let f = test_facet(&meta);
        assert!(f.is_err());
        Ok(())
    }

    #[test]
    fn test_facet_component_under_test() -> Result<(), Error> {
        let meta = json!({
          "facets": {
            "fuchsia.test": {
              "component_under_test": "fuchsia-pkg://fuchsia.com/test#meta/test.cmx"
            }
          }
        });
        let f = test_facet(&meta);
        assert!(!f.is_err());
        assert_eq!(f?.component_under_test, "fuchsia-pkg://fuchsia.com/test#meta/test.cmx");
        Ok(())
    }

    #[test]
    fn test_facet_component_under_test_not_string() -> Result<(), Error> {
        let meta = json!({
          "facets": {
            "fuchsia.test": {
              "component_under_test": 42
            }
          }
        });
        let f = test_facet(&meta);
        assert!(f.is_err());
        Ok(())
    }
}
