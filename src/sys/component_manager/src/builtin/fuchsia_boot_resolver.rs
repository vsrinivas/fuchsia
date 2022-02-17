// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        builtin::capability::BuiltinCapability,
        model::component::ComponentInstance,
        model::resolver::{self, ResolvedComponent, Resolver, ResolverError},
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{ClientEnd, Proxy},
    fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::boot_url::BootUrl,
    futures::TryStreamExt,
    routing::capability_source::InternalCapability,
    std::path::Path,
    std::sync::Arc,
};

pub static SCHEME: &str = "fuchsia-boot";

/// Resolves component URLs with the "fuchsia-boot" scheme, which supports loading components from
/// the /boot directory in component_manager's namespace.
///
/// On a typical system, this /boot directory is the bootfs served from the contents of the
/// 'ZBI_TYPE_STORAGE_BOOTFS' ZBI item by bootsvc, the process which starts component_manager.
///
/// For unit and integration tests, the /pkg directory in component_manager's namespace may be used
/// to load components.
///
/// URL syntax:
/// - fuchsia-boot:///path/within/bootfs#meta/component.cm
pub struct FuchsiaBootResolver {
    boot_proxy: DirectoryProxy,
}

impl FuchsiaBootResolver {
    /// Create a new FuchsiaBootResolver. This first checks whether the path passed in is present in
    /// the namespace, and returns Ok(None) if not present. For unit and integration tests, this
    /// path may point to /pkg.
    pub fn new(path: &'static str) -> Result<Option<FuchsiaBootResolver>, Error> {
        // Note that this check is synchronous. The async executor also likely is not being polled
        // yet, since this is called during startup.
        let bootfs_dir = Path::new(path);
        if !bootfs_dir.exists() {
            return Ok(None);
        }

        let proxy = io_util::open_directory_in_namespace(
            bootfs_dir.to_str().unwrap(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        Ok(Some(Self::new_from_directory(proxy)))
    }

    /// Create a new FuchsiaBootResolver that resolves URLs within the given directory. Used for
    /// injection in unit tests.
    fn new_from_directory(proxy: DirectoryProxy) -> FuchsiaBootResolver {
        FuchsiaBootResolver { boot_proxy: proxy }
    }

    async fn resolve_async(
        &self,
        component_url: &str,
    ) -> Result<fsys::Component, fsys::ResolverError> {
        // Parse URL.
        let url = BootUrl::parse(component_url).map_err(|_| fsys::ResolverError::InvalidArgs)?;

        // Package path is 'canonicalized' to ensure that it is relative, since absolute paths will
        // be (inconsistently) rejected by fuchsia.io methods.
        let package_path = io_util::canonicalize_path(url.path());
        let res = url.resource().ok_or(fsys::ResolverError::InvalidArgs)?;
        let cm_path = if package_path == "." {
            res.to_string()
        } else {
            Path::new(package_path).join(res).into_os_string().into_string().unwrap()
        };

        // Read the component manifest (.cm file) from the bootfs directory.
        let data = mem_util::open_file_data(&self.boot_proxy, &cm_path)
            .await
            .map_err(|_| fsys::ResolverError::ManifestNotFound)?;
        let decl_bytes = mem_util::bytes_from_data(&data).map_err(|_| fsys::ResolverError::Io)?;
        let decl: fdecl::Component = fidl::encoding::decode_persistent(&decl_bytes[..])
            .map_err(|_| fsys::ResolverError::InvalidManifest)?;

        let config_values = if let Some(config_decl) = decl.config.as_ref() {
            // if we have a config declaration, we need to read the value file from the package dir
            let strategy =
                config_decl.value_source.as_ref().ok_or(fsys::ResolverError::InvalidManifest)?;
            let config_path = match strategy {
                fdecl::ConfigValueSource::PackagePath(path) => path,
                fdecl::ConfigValueSourceUnknown!() => {
                    return Err(fsys::ResolverError::InvalidManifest);
                }
            };
            Some(
                mem_util::open_file_data(&self.boot_proxy, &config_path)
                    .await
                    .map_err(|_| fsys::ResolverError::ConfigValuesNotFound)?,
            )
        } else {
            None
        };

        // Set up the fuchsia-boot path as the component's "package" namespace.
        let path_proxy = io_util::directory::open_directory_no_describe(
            &self.boot_proxy,
            package_path,
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )
        .map_err(|_| fsys::ResolverError::Internal)?;

        Ok(fsys::Component {
            resolved_url: Some(component_url.into()),
            decl: Some(data),
            package: Some(fsys::Package {
                package_url: Some(url.root_url().to_string()),
                package_dir: Some(ClientEnd::new(
                    path_proxy.into_channel().unwrap().into_zx_channel(),
                )),
                ..fsys::Package::EMPTY
            }),
            config_values,
            ..fsys::Component::EMPTY
        })
    }
}

#[async_trait]
impl Resolver for FuchsiaBootResolver {
    async fn resolve(
        &self,
        component_url: &str,
        _target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        let fsys::Component { resolved_url, decl, package, config_values, .. } =
            self.resolve_async(component_url).await?;
        let resolved_url = resolved_url.unwrap();
        let decl = decl.ok_or_else(|| {
            ResolverError::ManifestInvalid(
                anyhow::format_err!("missing manifest from resolved component").into(),
            )
        })?;
        let decl = resolver::read_and_validate_manifest(&decl).await?;
        let config_values = if let Some(cv) = config_values {
            Some(resolver::read_and_validate_config_values(&cv)?)
        } else {
            None
        };
        Ok(ResolvedComponent { resolved_url, decl, package, config_values })
    }
}

#[async_trait]
impl BuiltinCapability for FuchsiaBootResolver {
    const NAME: &'static str = "boot_resolver";
    type Marker = fsys::ComponentResolverMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: fsys::ComponentResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(fsys::ComponentResolverRequest::Resolve { component_url, responder }) =
            stream.try_next().await?
        {
            responder.send(&mut self.resolve_async(&component_url).await)?;
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        match capability {
            InternalCapability::Resolver(name) if *name == Self::NAME => true,
            _ => false,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{component::ComponentInstance, environment::Environment},
        assert_matches::assert_matches,
        cm_rust::{FidlIntoNative, NativeIntoFidl},
        fidl::encoding::encode_persistent_with_context,
        fidl::endpoints::{create_proxy, ServerEnd},
        fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
        fidl_fuchsia_data as fdata,
        fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE},
        fuchsia_async::Task,
        io_util::directory::open_in_namespace,
        std::sync::Weak,
        vfs::{
            self, directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::asynchronous::read_only_static, pseudo_directory, remote::remote_dir,
        },
    };

    fn serve_vfs_dir(root: Arc<impl DirectoryEntry>) -> (Task<()>, DirectoryProxy) {
        let fs_scope = ExecutionScope::new();
        let (client, server) = create_proxy::<DirectoryMarker>().unwrap();
        root.open(
            fs_scope.clone(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server.into_channel()),
        );

        let vfs_task = Task::spawn(async move { fs_scope.wait().await });

        (vfs_task, client)
    }

    #[fuchsia::test]
    async fn hello_world_test() -> Result<(), Error> {
        let root = pseudo_directory! {
            "packages" => pseudo_directory! {
                "hello-world" => remote_dir(
                    open_in_namespace("/pkg", OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE).unwrap(),
                ),
            },
        };
        let (_task, bootfs) = serve_vfs_dir(root);
        let resolver = FuchsiaBootResolver::new_from_directory(bootfs);

        let root = ComponentInstance::new_root(
            Environment::empty(),
            Weak::new(),
            Weak::new(),
            "fuchsia-boot:///#meta/root.cm".to_string(),
        );

        let url = "fuchsia-boot:///packages/hello-world#meta/hello-world-rust.cm";
        let component = resolver.resolve(url, &root).await?;

        // Check that both the returned component manifest and the component manifest in
        // the returned package dir match the expected value. This also tests that
        // the resolver returned the right package dir.
        let ResolvedComponent { resolved_url, decl, package, .. } = component;
        assert_eq!(url, resolved_url);

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
        assert_eq!(package_url.unwrap(), "fuchsia-boot:///packages/hello-world");

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

        Ok(())
    }

    #[fuchsia::test]
    async fn config_works() {
        let fake_checksum = cm_rust::ConfigChecksum::Sha256([0; 32]);
        let mut manifest = fdecl::Component {
            config: Some(
                cm_rust::ConfigDecl {
                    value_source: cm_rust::ConfigValueSource::PackagePath(
                        "meta/has_config.cvf".to_string(),
                    ),
                    fields: vec![cm_rust::ConfigField {
                        key: "foo".to_string(),
                        type_: cm_rust::ConfigValueType::String { max_size: 100 },
                    }],
                    checksum: fake_checksum.clone(),
                }
                .native_into_fidl(),
            ),
            ..fdecl::Component::EMPTY
        };
        let mut values_data = fconfig::ValuesData {
            values: Some(vec![fconfig::ValueSpec {
                value: Some(fconfig::Value::Single(fconfig::SingleValue::Text(
                    "hello, world!".to_string(),
                ))),
                ..fconfig::ValueSpec::EMPTY
            }]),
            checksum: Some(fake_checksum.clone().native_into_fidl()),
            ..fconfig::ValuesData::EMPTY
        };
        let manifest_encoded = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut manifest,
        )
        .unwrap();
        let values_data_encoded = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut values_data,
        )
        .unwrap();
        let root = pseudo_directory! {
            "meta" => pseudo_directory! {
                "has_config.cm" => read_only_static(manifest_encoded),
                "has_config.cvf" => read_only_static(values_data_encoded),
            }
        };
        let (_task, bootfs) = serve_vfs_dir(root);
        let resolver = FuchsiaBootResolver::new_from_directory(bootfs);

        let root = ComponentInstance::new_root(
            Environment::empty(),
            Weak::new(),
            Weak::new(),
            "fuchsia-boot:///#meta/root.cm".to_string(),
        );

        let url = "fuchsia-boot:///#meta/has_config.cm";
        let component = resolver.resolve(url, &root).await.unwrap();

        let ResolvedComponent { resolved_url, decl, config_values, .. } = component;
        assert_eq!(url, resolved_url);

        let config_decl = decl.config.unwrap();
        let config_values = config_values.unwrap();

        let observed_fields =
            config_encoder::ConfigFields::resolve(&config_decl, config_values).unwrap();
        let expected_fields = config_encoder::ConfigFields {
            fields: vec![config_encoder::ConfigField {
                key: "foo".to_string(),
                value: cm_rust::Value::Single(cm_rust::SingleValue::Text(
                    "hello, world!".to_string(),
                )),
            }],
            checksum: fake_checksum,
        };
        assert_eq!(observed_fields, expected_fields);
    }

    #[fuchsia::test]
    async fn config_requires_values() {
        let mut manifest = fdecl::Component {
            config: Some(
                cm_rust::ConfigDecl {
                    value_source: cm_rust::ConfigValueSource::PackagePath(
                        "meta/has_config.cvf".to_string(),
                    ),
                    fields: vec![cm_rust::ConfigField {
                        key: "foo".to_string(),
                        type_: cm_rust::ConfigValueType::String { max_size: 100 },
                    }],
                    checksum: cm_rust::ConfigChecksum::Sha256([0; 32]),
                }
                .native_into_fidl(),
            ),
            ..fdecl::Component::EMPTY
        };
        let manifest_encoded = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut manifest,
        )
        .unwrap();
        let root = pseudo_directory! {
            "meta" => pseudo_directory! {
                "has_config.cm" => read_only_static(manifest_encoded),
            }
        };
        let (_task, bootfs) = serve_vfs_dir(root);
        let resolver = FuchsiaBootResolver::new_from_directory(bootfs);

        let root = ComponentInstance::new_root(
            Environment::empty(),
            Weak::new(),
            Weak::new(),
            "fuchsia-boot:///#meta/root.cm".to_string(),
        );

        let url = "fuchsia-boot:///#meta/has_config.cm";
        let err = resolver.resolve(url, &root).await.unwrap_err();
        assert_matches!(err, ResolverError::ConfigValuesIo { .. });
    }

    macro_rules! test_resolve_error {
        ($resolver:ident, $url:expr, $target:ident, $resolver_error_expected:ident) => {
            let res = $resolver.resolve($url, &$target).await;
            match res.err().expect("unexpected success") {
                ResolverError::$resolver_error_expected { .. } => {}
                e => panic!("unexpected error {:?}", e),
            }
        };
    }

    #[fuchsia::test]
    async fn resolve_errors_test() {
        let manifest_encoded = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut fdecl::Component {
                program: Some(fdecl::Program {
                    runner: None,
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..fdecl::Program::EMPTY
                }),
                ..fdecl::Component::EMPTY
            },
        )
        .unwrap();
        let root = pseudo_directory! {
            "meta" => pseudo_directory! {
                // Provide a cm that will fail due to a missing runner.
                "invalid.cm" => read_only_static(manifest_encoded),
            },
        };
        let (_task, bootfs) = serve_vfs_dir(root);
        let resolver = FuchsiaBootResolver::new_from_directory(bootfs);
        let root = ComponentInstance::new_root(
            Environment::empty(),
            Weak::new(),
            Weak::new(),
            "fuchsia-boot:///#meta/root.cm".to_string(),
        );
        test_resolve_error!(resolver, "fuchsia-boot:///#meta/invalid.cm", root, ManifestInvalid);
    }
}
