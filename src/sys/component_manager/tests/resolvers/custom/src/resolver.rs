// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_component_resolution as fresolution,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio, fidl_fuchsia_mem as fmem,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    tracing::*,
};

const TEST_COMPONENT_RESOLVED_URL: &str =
    "fuchsia-pkg://fuchsia.com/component-manager-test-resolver#meta/trigger.cm";
const TEST_COMPONENT_PACKAGE_URL: &str =
    "fuchsia-pkg://fuchsia.com/component-manager-test-resolver";

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    /// A request to the fuchsia.component.resolution.Resolver protocol.
    ResolverProtocol(fresolution::ResolverRequestStream),
    /// A request to the fuchsia.sys2.ComponentResolver protocol.
    InternalResolverProtocol(fsys::ComponentResolverRequestStream),
}

async fn serve_resolver(mut stream: fresolution::ResolverRequestStream) -> Result<(), Error> {
    while let Some(request) =
        stream.try_next().await.context("failed to read request from stream")?
    {
        match request {
            fresolution::ResolverRequest::Resolve { component_url, responder } => {
                if component_url == "test://trigger" {
                    let (client, server) = fidl::endpoints::create_endpoints()
                        .context("failed to create zx::channel pair")?;
                    fdio::open(
                        "/pkg",
                        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
                        server.into_channel(),
                    )
                    .context("failed to open /pkg")?;
                    responder.send(&mut Ok(fresolution::Component {
                        url: Some("fuchsia-pkg://fuchsia.com/component-manager-test-resolver#meta/component.cm".to_string()),
                        decl: Some(build_decl()),
                        package: Some(fresolution::Package {
                            url: Some("fuchsia-pkg://fuchsia.com/component-manager-test-resolver".to_string()),
                            directory: Some(client),
                            ..fresolution::Package::EMPTY
                        }),
                        ..fresolution::Component::EMPTY
                    })).with_context(|| format!("failed to send response to resolve request for component URL {}", component_url))?;
                } else {
                    responder
                        .send(&mut Err(fresolution::ResolverError::ManifestNotFound))
                        .with_context(|| {
                            format!(
                                "failed to send response to resolve request for component URL {}",
                                component_url
                            )
                        })?;
                }
            }
        }
    }
    Ok(())
}

async fn serve_internal_resolver(
    mut stream: fsys::ComponentResolverRequestStream,
) -> Result<(), Error> {
    while let Some(request) =
        stream.try_next().await.context("failed to read request from stream")?
    {
        match request {
            fsys::ComponentResolverRequest::Resolve { component_url, responder } => {
                if component_url == "test-internal://trigger" {
                    let (client, server) = fidl::endpoints::create_endpoints()
                        .context("failed to create zx::channel pair")?;
                    fdio::open(
                        "/pkg",
                        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
                        server.into_channel(),
                    )
                    .context("failed to open /pkg")?;
                    responder
                        .send(&mut Ok(fsys::Component {
                            resolved_url: Some(TEST_COMPONENT_RESOLVED_URL.to_string()),
                            decl: Some(build_decl()),
                            package: Some(fsys::Package {
                                package_url: Some(TEST_COMPONENT_PACKAGE_URL.to_string()),
                                package_dir: Some(client),
                                ..fsys::Package::EMPTY
                            }),
                            ..fsys::Component::EMPTY
                        }))
                        .with_context(|| {
                            format!(
                                "failed to send response to resolve request for component URL {}",
                                component_url
                            )
                        })?;
                } else {
                    responder.send(&mut Err(fsys::ResolverError::ManifestNotFound)).with_context(
                        || {
                            format!(
                                "failed to send response to resolve request for component URL {}",
                                component_url
                            )
                        },
                    )?;
                }
            }
        }
    }
    Ok(())
}

fn build_decl() -> fmem::Data {
    let mut component_decl = fdecl::Component::EMPTY;
    component_decl.program = Some(fdecl::Program {
        runner: Some("elf".to_string()),
        info: Some(fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: "binary".to_string(),
                value: Some(Box::new(fdata::DictionaryValue::Str(
                    "bin/component_manager_test_trigger_bin".to_string(),
                ))),
            }]),
            ..fdata::Dictionary::EMPTY
        }),
        ..fdecl::Program::EMPTY
    });
    component_decl.capabilities = Some(vec![fdecl::Capability::Protocol(fdecl::Protocol {
        name: Some("fidl.test.components.Trigger".to_string()),
        source_path: Some("/svc/fidl.test.components.Trigger".to_string()),
        ..fdecl::Protocol::EMPTY
    })]);
    component_decl.exposes = Some(vec![fdecl::Expose::Protocol(fdecl::ExposeProtocol {
        source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
        target: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
        source_name: Some("fidl.test.components.Trigger".to_string()),
        target_name: Some("fidl.test.components.Trigger".to_string()),
        ..fdecl::ExposeProtocol::EMPTY
    })]);
    fmem::Data::Bytes(
        fidl::encoding::encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut component_decl,
        )
        .expect("encoded"),
    )
}

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut service_fs = ServiceFs::new_local();
    service_fs
        .dir("svc")
        .add_fidl_service(IncomingRequest::ResolverProtocol)
        .add_fidl_service(IncomingRequest::InternalResolverProtocol);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::ResolverProtocol(stream) => match serve_resolver(stream).await {
                    Ok(()) => {}
                    Err(err) => error!("resolver failed: {}", err),
                },
                IncomingRequest::InternalResolverProtocol(stream) => {
                    match serve_internal_resolver(stream).await {
                        Ok(()) => {}
                        Err(err) => error!("internal resolver failed: {}", err),
                    }
                }
            }
        })
        .await;

    Ok(())
}
