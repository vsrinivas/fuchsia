// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::ComponentInstance,
        resolver::{ResolvedComponent, Resolver, ResolverError},
    },
    anyhow::format_err,
    async_trait::async_trait,
    cm_fidl_validator,
    cm_rust::FidlIntoNative,
    fidl::endpoints::{ClientEnd, Proxy},
    fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io::{self as fio, DirectoryMarker},
    fidl_fuchsia_sys::LoaderProxy,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::pkg_url::PkgUrl,
    std::path::Path,
    std::sync::Arc,
};

#[allow(unused)]
pub static SCHEME: &str = "fuchsia-pkg";

/// Resolves component URLs with the "fuchsia-pkg" scheme by proxying to an existing
/// fuchsia.sys.Loader service (which is the CFv1 equivalent of fuchsia.sys2.ComponentResolver).
///
/// This resolver implementation is used to bridge the v1 and v2 component runtime worlds in
/// situations where the v2 runtime runs under the v1 runtime.
///
/// See the fuchsia_pkg_url crate for URL syntax.
pub struct FuchsiaPkgResolver {
    loader: LoaderProxy,
}

impl FuchsiaPkgResolver {
    pub fn new(loader: LoaderProxy) -> FuchsiaPkgResolver {
        FuchsiaPkgResolver { loader }
    }

    async fn resolve_async<'a>(
        &'a self,
        component_url: &'a str,
    ) -> Result<ResolvedComponent, ResolverError> {
        // Parse URL.
        let fuchsia_pkg_url =
            PkgUrl::parse(component_url).map_err(|e| ResolverError::malformed_url(e))?;
        let cm_path =
            Path::new(fuchsia_pkg_url.resource().ok_or(ResolverError::UrlMissingResource)?);
        let package_url = fuchsia_pkg_url.root_url().to_string();

        // Resolve package.
        let package = self
            .loader
            .load_url(&package_url)
            .await
            .map_err(|e| ResolverError::package_not_found(e))?
            .ok_or(ResolverError::package_not_found(format_err!("package not available")))?;
        let dir = package.directory.ok_or(ResolverError::package_not_found(format_err!(
            "package is missing directory handle"
        )))?;

        // Read component manifest from package.
        let dir = ClientEnd::<DirectoryMarker>::new(dir)
            .into_proxy()
            .expect("failed to create directory proxy");
        let file = io_util::open_file(&dir, cm_path, fio::OPEN_RIGHT_READABLE)
            .map_err(|e| ResolverError::manifest_not_found(e))?;
        let component_decl = io_util::read_file_fidl(&file).await.map_err(|e| {
            match e.downcast_ref::<io_util::file::ReadError>() {
                Some(_) => ResolverError::manifest_not_found(e),
                None => ResolverError::manifest_invalid(e),
            }
        })?;
        // Validate the component manifest
        cm_fidl_validator::validate(&component_decl)
            .map_err(|e| ResolverError::manifest_invalid(e))?;

        // Get config values from the package if needed
        let config_values = if let Some(config_decl) = &component_decl.config {
            let strategy = config_decl.value_source.as_ref().ok_or_else(|| {
                ResolverError::manifest_invalid(anyhow::format_err!(
                    "missing a strategy for resolving config values"
                ))
            })?;
            let config_path = match strategy {
                fdecl::ConfigValueSource::PackagePath(path) => Path::new(path),
                other => {
                    return Err(ResolverError::manifest_invalid(anyhow::format_err!(
                        "unrecognized config value strategy: {:?}",
                        other
                    )))
                }
            };
            let file = io_util::open_file(&dir, config_path, fio::OPEN_RIGHT_READABLE)
                .map_err(|e| ResolverError::Io(e.into()))?;
            let values_data =
                io_util::read_file_fidl(&file).await.map_err(|e| ResolverError::Io(e.into()))?;
            cm_fidl_validator::validate_values_data(&values_data)
                .map_err(|e| ResolverError::config_values_invalid(e))?;
            Some(values_data.fidl_into_native())
        } else {
            None
        };

        let package_dir = ClientEnd::new(
            dir.into_channel().expect("could not convert proxy to channel").into_zx_channel(),
        );
        let package = fsys::Package {
            package_url: Some(package_url),
            package_dir: Some(package_dir),
            ..fsys::Package::EMPTY
        };
        Ok(ResolvedComponent {
            resolved_url: component_url.to_string(),
            decl: component_decl.fidl_into_native(),
            package: Some(package),
            config_values,
        })
    }
}

#[async_trait]
impl Resolver for FuchsiaPkgResolver {
    async fn resolve(
        &self,
        component_url: &str,
        _target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        self.resolve_async(component_url).await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_rust::FidlIntoNative,
        fidl::encoding::encode_persistent_with_context,
        fidl::endpoints::{self, ServerEnd},
        fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
        fidl_fuchsia_data as fdata,
        fidl_fuchsia_sys::{LoaderMarker, LoaderRequest, Package},
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::TryStreamExt,
        std::path::Path,
        vfs::{
            self, directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::asynchronous::read_only_static, pseudo_directory,
        },
    };

    struct MockLoader {}

    impl MockLoader {
        fn start() -> LoaderProxy {
            let (proxy, server): (_, ServerEnd<LoaderMarker>) = endpoints::create_proxy().unwrap();
            fasync::Task::local(async move {
                let loader = MockLoader {};
                let mut stream = server.into_stream().unwrap();
                while let Some(LoaderRequest::LoadUrl { url, responder }) =
                    stream.try_next().await.expect("failed to read request")
                {
                    let mut package = loader.load_url(&url);
                    let package = package.as_mut();
                    responder.send(package).expect("responder failed");
                }
            })
            .detach();
            proxy
        }

        // TODO(fxbug.dev/37534): This can be simplified to no longer need to use the test's real package
        // directory once Rust vfs supports OPEN_RIGHT_EXECUTABLE.
        fn load_url(&self, package_url: &str) -> Option<Package> {
            let (dir_c, dir_s) = zx::Channel::create().unwrap();
            let parsed_url = PkgUrl::parse(&package_url).expect("bad url");
            // Simulate a package server that only contains the "hello-world" package.
            let invalid_cm_bytes = encode_persistent_with_context(
                &fidl::encoding::Context {
                    wire_format_version: fidl::encoding::WireFormatVersion::V2,
                },
                &mut fdecl::Component {
                    program: Some(fdecl::Program {
                        runner: None,
                        info: Some(fdata::Dictionary {
                            entries: Some(vec![]),
                            ..fdata::Dictionary::EMPTY
                        }),
                        ..fdecl::Program::EMPTY
                    }),
                    uses: None,
                    exposes: None,
                    offers: None,
                    capabilities: None,
                    children: None,
                    collections: None,
                    environments: None,
                    facets: None,
                    ..fdecl::Component::EMPTY
                },
            )
            .unwrap();
            let foo_cm_bytes = encode_persistent_with_context(
                &fidl::encoding::Context {
                    wire_format_version: fidl::encoding::WireFormatVersion::V2,
                },
                &mut fdecl::Component {
                    config: Some(fdecl::ConfigSchema {
                        fields: Some(vec![fdecl::ConfigField {
                            key: Some("test".to_string()),
                            type_: Some(fdecl::ConfigType {
                                layout: fdecl::ConfigTypeLayout::Bool,
                                parameters: Some(vec![]),
                                constraints: vec![],
                            }),
                            ..fdecl::ConfigField::EMPTY
                        }]),
                        checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                        value_source: Some(fdecl::ConfigValueSource::PackagePath(
                            "config/foo.cvf".to_string(),
                        )),
                        ..fdecl::ConfigSchema::EMPTY
                    }),
                    ..fdecl::Component::EMPTY
                },
            )
            .unwrap();
            let foo_cvf_bytes = encode_persistent_with_context(
                &fidl::encoding::Context {
                    wire_format_version: fidl::encoding::WireFormatVersion::V2,
                },
                &mut fconfig::ValuesData {
                    values: Some(vec![fconfig::ValueSpec {
                        value: Some(fconfig::Value::Single(fconfig::SingleValue::Flag(false))),
                        ..fconfig::ValueSpec::EMPTY
                    }]),
                    checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
                    ..fconfig::ValuesData::EMPTY
                },
            )
            .unwrap();
            match parsed_url.name().as_ref() {
                "hello-world" => {
                    let path = Path::new("/pkg");
                    io_util::connect_in_namespace(
                        path.to_str().unwrap(),
                        dir_s,
                        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                    )
                    .expect("could not connect to /pkg");
                    return Some(Package {
                        data: None,
                        directory: Some(dir_c),
                        resolved_url: package_url.to_string(),
                    });
                }
                "invalid-cm" => {
                    // Provide a cm that will fail due to a missing runner.
                    let sub_dir = pseudo_directory! {
                        "meta" => pseudo_directory! {
                            "invalid.cm" => read_only_static(invalid_cm_bytes),
                        }
                    };
                    sub_dir.open(
                        ExecutionScope::new(),
                        fio::OPEN_RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        vfs::path::Path::dot(),
                        ServerEnd::new(dir_s),
                    );
                    return Some(Package {
                        data: None,
                        directory: Some(dir_c),
                        resolved_url: package_url.to_string(),
                    });
                }
                "structured-config" => {
                    // Provide a cm that will fail due to a missing runner.
                    let sub_dir = pseudo_directory! {
                        "meta" => pseudo_directory! {
                            "foo.cm" => read_only_static(foo_cm_bytes),
                        },
                        "config" => pseudo_directory! {
                            "foo.cvf" => read_only_static(foo_cvf_bytes),
                        }
                    };
                    sub_dir.open(
                        ExecutionScope::new(),
                        fio::OPEN_RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        vfs::path::Path::dot(),
                        ServerEnd::new(dir_s),
                    );
                    return Some(Package {
                        data: None,
                        directory: Some(dir_c),
                        resolved_url: package_url.to_string(),
                    });
                }
                _ => return None,
            }
        }
    }

    #[fuchsia::test]
    async fn resolve_test() {
        let loader = MockLoader::start();
        let resolver = FuchsiaPkgResolver::new(loader);
        let url = "fuchsia-pkg://fuchsia.com/hello-world#meta/hello-world-rust.cm";
        let component = resolver.resolve_async(url).await.expect("resolve failed");

        // Check that both the returned component manifest and the component manifest in
        // the returned package dir match the expected value. This also tests that
        // the resolver returned the right package dir.
        let ResolvedComponent { resolved_url, decl, package, .. } = component;
        assert_eq!(resolved_url, url);

        let expected_program = Some(cm_rust::ProgramDecl {
            runner: Some("elf".into()),
            info: fdata::Dictionary {
                entries: Some(vec![
                    fdata::DictionaryEntry {
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(
                            "bin/hello_world_rust".to_string(),
                        ))),
                    },
                    fdata::DictionaryEntry {
                        key: "forward_stderr_to".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str("log".to_string()))),
                    },
                    fdata::DictionaryEntry {
                        key: "forward_stdout_to".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str("log".to_string()))),
                    },
                ]),
                ..fdata::Dictionary::EMPTY
            },
        });

        // no need to check full decl as we just want to make
        // sure that we were able to resolve.
        assert_eq!(decl.program, expected_program);

        let fsys::Package { package_url, package_dir, .. } = package.unwrap();
        assert_eq!(package_url.unwrap(), "fuchsia-pkg://fuchsia.com/hello-world");

        let dir_proxy = package_dir.unwrap().into_proxy().unwrap();
        let path = Path::new("meta/hello-world-rust.cm");
        let file_proxy = io_util::open_file(&dir_proxy, path, fio::OPEN_RIGHT_READABLE)
            .expect("could not open cm");

        let decl = io_util::read_file_fidl::<fdecl::Component>(&file_proxy)
            .await
            .expect("could not read cm");
        let decl = decl.fidl_into_native();

        assert_eq!(decl.program, expected_program);

        // Try to load an executable file, like a binary, reusing the library_loader helper that
        // opens with OPEN_RIGHT_EXECUTABLE and gets a VMO with VMO_FLAG_EXEC.
        library_loader::load_vmo(&dir_proxy, "bin/hello_world_rust")
            .await
            .expect("failed to open executable file");
    }

    #[fuchsia::test]
    async fn structured_config() {
        let loader = MockLoader::start();
        let resolver = FuchsiaPkgResolver::new(loader);
        let url = "fuchsia-pkg://fuchsia.com/structured-config#meta/foo.cm";
        let component = resolver.resolve_async(url).await.expect("resolve failed");

        let ResolvedComponent { decl, config_values, .. } = component;

        let expected_config = Some(cm_rust::ConfigDecl {
            fields: vec![cm_rust::ConfigField {
                key: "test".to_string(),
                type_: cm_rust::ConfigValueType::Bool,
            }],
            checksum: cm_rust::ConfigChecksum::Sha256([0; 32]),
            value_source: cm_rust::ConfigValueSource::PackagePath("config/foo.cvf".to_string()),
        });

        assert_eq!(decl.config, expected_config);

        let expected_values = Some(cm_rust::ValuesData {
            values: vec![cm_rust::ValueSpec {
                value: cm_rust::Value::Single(cm_rust::SingleValue::Flag(false)),
            }],
            checksum: cm_rust::ConfigChecksum::Sha256([0; 32]),
        });

        assert_eq!(config_values, expected_values);
    }

    macro_rules! test_resolve_error {
        ($resolver:ident, $url:expr, $resolver_error_expected:pat) => {
            let res = $resolver.resolve_async($url).await;
            match res.err().expect("unexpected success") {
                $resolver_error_expected => {}
                e => panic!("unexpected error {:?}", e),
            }
        };
    }

    #[fuchsia::test]
    async fn resolve_errors_test() {
        let loader = MockLoader::start();
        let resolver = FuchsiaPkgResolver::new(loader);
        test_resolve_error!(
            resolver,
            "fuchsia-pkg:///hello-world#meta/hello-world-rust.cm",
            ResolverError::MalformedUrl { .. }
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello-world",
            ResolverError::UrlMissingResource
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/goodbye-world#meta/hello-world-rust.cm",
            ResolverError::PackageNotFound { .. }
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello-world#meta/does_not_exist.cm",
            ResolverError::ManifestNotFound { .. }
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello-world#meta/component_manager_tests.invalid_cm",
            ResolverError::ManifestInvalid { .. }
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/invalid-cm#meta/invalid.cm",
            ResolverError::ManifestInvalid { .. }
        );
    }
}
