// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::{self as fsys, ComponentResolverRequest, ComponentResolverRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::Status,
    futures::prelude::*,
    log::*,
};

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    /// A request to the fuchsia.sys2.ComponentResolver protocol.
    ResolverProtocol(ComponentResolverRequestStream),
}

async fn serve_resolver(mut stream: ComponentResolverRequestStream) -> Result<(), Error> {
    while let Some(request) =
        stream.try_next().await.context("failed to read request from stream")?
    {
        match request {
            ComponentResolverRequest::Resolve { component_url, responder } => {
                if component_url == "test://component" {
                    let (client, server) = fidl::endpoints::create_endpoints()
                        .context("failed to create zx::channel pair")?;
                    fdio::open("/pkg", fio::OPEN_RIGHT_READABLE, server.into_channel())
                        .context("failed to open /pkg")?;
                    responder.send(Status::OK.into_raw(), fsys::Component {
                        resolved_url: Some("fuchsia-pkg://fuchsia.com/component-manager-test-resolver#meta/component.cm".to_string()),
                        decl: Some(build_decl()),
                        package: Some(fsys::Package {
                            package_url: Some("fuchsia-pkg://fuchsia.com/component-manager-test-resolver".to_string()),
                            package_dir: Some(client),
                            ..fsys::Package::empty()
                        }),
                        ..fsys::Component::empty()
                    }).with_context(|| format!("failed to send response to resolve request for component URL {}", component_url))?;
                } else {
                    responder
                        .send(
                            Status::NOT_FOUND.into_raw(),
                            fsys::Component {
                                resolved_url: None,
                                decl: None,
                                package: None,
                                ..fsys::Component::empty()
                            },
                        )
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

fn build_decl() -> fsys::ComponentDecl {
    let mut component_decl = fsys::ComponentDecl::empty();
    component_decl.program = Some(fdata::Dictionary {
        entries: Some(vec![fdata::DictionaryEntry {
            key: "binary".to_string(),
            value: Some(Box::new(fdata::DictionaryValue::Str(
                "bin/component_manager_test_resolvers_component".to_string(),
            ))),
        }]),
        ..fdata::Dictionary::empty()
    });
    component_decl.capabilities = Some(vec![fsys::CapabilityDecl::Protocol(fsys::ProtocolDecl {
        name: Some("fidl.test.components.Trigger".to_string()),
        source_path: Some("/svc/fidl.test.components.Trigger".to_string()),
        ..fsys::ProtocolDecl::empty()
    })]);
    component_decl.uses = Some(vec![fsys::UseDecl::Runner(fsys::UseRunnerDecl {
        source_name: Some("elf".to_string()),
        ..fsys::UseRunnerDecl::empty()
    })]);
    component_decl.exposes = Some(vec![fsys::ExposeDecl::Protocol(fsys::ExposeProtocolDecl {
        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
        target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
        source_name: Some("fidl.test.components.Trigger".to_string()),
        target_name: Some("fidl.test.components.Trigger".to_string()),
        ..fsys::ExposeProtocolDecl::empty()
    })]);
    component_decl
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().expect("failed to initialize logging");
    let mut service_fs = ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(IncomingRequest::ResolverProtocol);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::ResolverProtocol(stream) => match serve_resolver(stream).await {
                    Ok(()) => {}
                    Err(err) => error!("resolver failed: {}", err),
                },
            }
        })
        .await;

    Ok(())
}
