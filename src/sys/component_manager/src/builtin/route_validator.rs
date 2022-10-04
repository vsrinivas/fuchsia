// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, PERMITTED_FLAGS},
        model::{
            component::{ComponentInstance, InstanceState},
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
            routing::{
                request_for_namespace_capability_expose, request_for_namespace_capability_use,
                route,
            },
        },
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, ExposeDecl, ExposeDeclCommon, SourceName, UseDecl},
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::endpoints::{DiscoverableProtocolMarker, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::StreamExt,
    lazy_static::lazy_static,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker},
    std::{
        convert::TryFrom,
        path::PathBuf,
        sync::{Arc, Weak},
    },
    tracing::warn,
};

lazy_static! {
    pub static ref ROUTE_VALIDATOR_CAPABILITY_NAME: CapabilityName =
        fsys::RouteValidatorMarker::PROTOCOL_NAME.into();
}

/// Serves the fuchsia.sys2.RouteValidator protocol.
pub struct RouteValidator {
    model: Arc<Model>,
}

impl RouteValidator {
    pub fn new(model: Arc<Model>) -> Self {
        Self { model }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "RouteValidator",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Given a `CapabilitySource`, determine if it is a framework-provided
    /// RouteValidator capability. If so, serve the capability.
    async fn on_capability_routed_async(
        self: Arc<Self>,
        source: CapabilitySource,
        capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
    ) -> Result<(), ModelError> {
        // If this is a scoped framework directory capability, then check the source path
        if let CapabilitySource::Framework { capability, component } = source {
            if capability.matches_protocol(&ROUTE_VALIDATOR_CAPABILITY_NAME) {
                // Set the capability provider, if not already set.
                let mut capability_provider = capability_provider.lock().await;
                if capability_provider.is_none() {
                    *capability_provider = Some(Box::new(RouteValidatorCapabilityProvider::query(
                        self,
                        component.abs_moniker.clone(),
                    )));
                }
            }
        }
        Ok(())
    }

    async fn validate(
        self: &Arc<Self>,
        scope_moniker: &AbsoluteMoniker,
        moniker_str: String,
    ) -> Result<Vec<fsys::RouteReport>, fcomponent::Error> {
        // Construct the complete moniker using the scope moniker and the relative moniker string.
        let moniker = join_monikers(scope_moniker, &moniker_str)?;

        let instance =
            self.model.find(&moniker).await.ok_or(fcomponent::Error::InstanceNotFound)?;

        // Get all use and expose declarations for this component
        let (uses, exposes) = {
            let state = instance.lock_state().await;

            let resolved = match *state {
                InstanceState::Resolved(ref r) => r,
                // TODO(http://fxbug.dev/102026): The error is that the instance is not currently
                // resolved. Use a better error here, when one exists.
                _ => return Err(fcomponent::Error::InstanceCannotResolve),
            };

            let uses = resolved.decl().uses.clone();
            let exposes = resolved.decl().exposes.clone();

            (uses, exposes)
        };

        let mut reports = validate_uses(uses, &instance).await;
        let mut expose_reports = validate_exposes(exposes, &instance).await;
        reports.append(&mut expose_reports);

        Ok(reports)
    }

    /// Serve the fuchsia.sys2.RouteValidator protocol for a given scope on a given stream
    async fn serve(
        self: Arc<Self>,
        scope_moniker: AbsoluteMoniker,
        mut stream: fsys::RouteValidatorRequestStream,
    ) {
        loop {
            let fsys::RouteValidatorRequest::Validate { moniker, responder } =
                match stream.next().await {
                    Some(Ok(request)) => request,
                    Some(Err(e)) => {
                        warn!(error = %e, "Could not get next RouteValidator request");
                        break;
                    }
                    None => break,
                };

            let mut result = self.validate(&scope_moniker, moniker).await;
            if let Err(e) = responder.send(&mut result) {
                warn!(error = %e, "RouteValidator failed to respond to Validate call");
                break;
            }
        }
    }
}

#[async_trait]
impl Hook for RouteValidator {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::CapabilityRouted { source, capability_provider }) => {
                self.on_capability_routed_async(source.clone(), capability_provider.clone())
                    .await?;
            }
            _ => {}
        }
        Ok(())
    }
}

pub struct RouteValidatorCapabilityProvider {
    query: Arc<RouteValidator>,
    scope_moniker: AbsoluteMoniker,
}

impl RouteValidatorCapabilityProvider {
    pub fn query(query: Arc<RouteValidator>, scope_moniker: AbsoluteMoniker) -> Self {
        Self { query, scope_moniker }
    }
}

#[async_trait]
impl CapabilityProvider for RouteValidatorCapabilityProvider {
    async fn open(
        self: Box<Self>,
        task_scope: TaskScope,
        flags: fio::OpenFlags,
        _open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let forbidden = flags - PERMITTED_FLAGS;
        if !forbidden.is_empty() {
            warn!(?forbidden, "RouteValidator capability");
            return Ok(());
        }

        if relative_path.components().count() != 0 {
            warn!(
                path=%relative_path.display(),
                "RouteValidator capability got open request with non-empty",
            );
            return Ok(());
        }

        let server_end = channel::take_channel(server_end);

        let server_end = ServerEnd::<fsys::RouteValidatorMarker>::new(server_end);
        let stream: fsys::RouteValidatorRequestStream =
            server_end.into_stream().map_err(ModelError::stream_creation_error)?;
        task_scope
            .add_task(async move {
                self.query.serve(self.scope_moniker, stream).await;
            })
            .await;

        Ok(())
    }
}

/// Takes the scoped component's moniker and a relative moniker string and join them into an
/// absolute moniker.
fn join_monikers(
    scope_moniker: &AbsoluteMoniker,
    relative_moniker_str: &str,
) -> Result<AbsoluteMoniker, fcomponent::Error> {
    let relative_moniker = RelativeMoniker::try_from(relative_moniker_str)
        .map_err(|_| fcomponent::Error::InvalidArguments)?;
    let abs_moniker = scope_moniker.descendant(&relative_moniker);
    Ok(abs_moniker)
}

async fn validate_uses(
    uses: Vec<UseDecl>,
    instance: &Arc<ComponentInstance>,
) -> Vec<fsys::RouteReport> {
    let mut reports = vec![];
    for use_ in uses {
        let capability = Some(use_.source_name().to_string());
        let decl_type = Some(fsys::DeclType::Use);
        if let Ok(route_request) = request_for_namespace_capability_use(use_) {
            let error = if let Err(e) = route(route_request, &instance).await {
                Some(fsys::RouteError { summary: Some(e.to_string()), ..fsys::RouteError::EMPTY })
            } else {
                None
            };

            reports.push(fsys::RouteReport {
                capability,
                decl_type,
                error,
                ..fsys::RouteReport::EMPTY
            })
        }
    }
    reports
}

async fn validate_exposes(
    exposes: Vec<ExposeDecl>,
    instance: &Arc<ComponentInstance>,
) -> Vec<fsys::RouteReport> {
    let mut reports = vec![];
    for expose in exposes {
        let capability = Some(expose.target_name().to_string());
        let decl_type = Some(fsys::DeclType::Expose);
        if let Ok(route_request) = request_for_namespace_capability_expose(expose) {
            let error = if let Err(e) = route(route_request, instance).await {
                Some(fsys::RouteError { summary: Some(e.to_string()), ..fsys::RouteError::EMPTY })
            } else {
                None
            };

            reports.push(fsys::RouteReport {
                capability,
                decl_type,
                error,
                ..fsys::RouteReport::EMPTY
            })
        }
    }
    reports
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::testing::test_helpers::{TestEnvironmentBuilder, TestModelResult},
        cm_rust::*,
        cm_rust_testing::ComponentDeclBuilder,
        fidl::endpoints::create_proxy_and_stream,
        fuchsia_async as fasync,
    };

    #[derive(Ord, PartialOrd, Eq, PartialEq)]
    struct Key {
        capability: String,
        decl_type: fsys::DeclType,
    }

    #[fuchsia::test]
    async fn all_routes_success() {
        let use_from_framework_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Framework,
            source_name: "fuchsia.component.Realm".into(),
            target_path: CapabilityPath::try_from("/svc/fuchsia.component.Realm").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let use_from_child_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Child("my_child".to_string()),
            source_name: "foo.bar".into(),
            target_path: CapabilityPath::try_from("/svc/foo.bar").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let expose_from_child_decl = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child("my_child".to_string()),
            source_name: "foo.bar".into(),
            target: ExposeTarget::Parent,
            target_name: "foo.bar".into(),
        });

        let expose_from_self_decl = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: "foo.bar".into(),
            target: ExposeTarget::Parent,
            target_name: "foo.bar".into(),
        });

        let capability_decl = ProtocolDecl {
            name: "foo.bar".into(),
            source_path: Some(CapabilityPath::try_from("/svc/foo.bar").unwrap()),
        };

        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .use_(use_from_framework_decl)
                    .use_(use_from_child_decl)
                    .expose(expose_from_child_decl)
                    .add_lazy_child("my_child")
                    .build(),
            ),
            (
                "my_child",
                ComponentDeclBuilder::new()
                    .protocol(capability_decl)
                    .expose(expose_from_self_decl)
                    .build(),
            ),
        ];

        let TestModelResult { model, builtin_environment, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let route_validator = {
            let env = builtin_environment.lock().await;
            env.route_validator.clone().unwrap()
        };

        let (validator, validator_request_stream) =
            create_proxy_and_stream::<fsys::RouteValidatorMarker>().unwrap();

        let _validator_task = fasync::Task::local(async move {
            route_validator.serve(AbsoluteMoniker::root(), validator_request_stream).await
        });

        model.start().await;

        // `my_child` should not be resolved right now
        let instance = model.find_resolved(&vec!["my_child"].into()).await;
        assert!(instance.is_none());

        // Validate the root
        let mut results = validator.validate(".").await.unwrap().unwrap();

        results.sort_by_key(|r| Key {
            capability: r.capability.clone().unwrap(),
            decl_type: r.decl_type.clone().unwrap(),
        });

        assert_eq!(results.len(), 3);

        let report = results.remove(0);
        assert_eq!(report.capability.unwrap(), "foo.bar");
        assert_eq!(report.decl_type.unwrap(), fsys::DeclType::Use);
        assert!(report.error.is_none());

        let report = results.remove(0);
        assert_eq!(report.capability.unwrap(), "foo.bar");
        assert_eq!(report.decl_type.unwrap(), fsys::DeclType::Expose);
        assert!(report.error.is_none());

        let report = results.remove(0);
        assert_eq!(report.capability.unwrap(), "fuchsia.component.Realm");
        assert_eq!(report.decl_type.unwrap(), fsys::DeclType::Use);
        assert!(report.error.is_none());

        // This validation should have caused `my_child` to be resolved
        let instance = model.find_resolved(&vec!["my_child"].into()).await;
        assert!(instance.is_some());

        // Validate `my_child`
        let mut results = validator.validate("./my_child").await.unwrap().unwrap();
        assert_eq!(results.len(), 1);

        let report = results.remove(0);
        assert_eq!(report.capability.unwrap(), "foo.bar");
        assert_eq!(report.decl_type.unwrap(), fsys::DeclType::Expose);
        assert!(report.error.is_none());
    }

    #[fuchsia::test]
    async fn all_routes_fail() {
        let invalid_source_name_use_from_child_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Child("my_child".to_string()),
            source_name: "a".into(),
            target_path: CapabilityPath::try_from("/svc/a").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let invalid_source_use_from_child_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Child("bad_child".to_string()),
            source_name: "b".into(),
            target_path: CapabilityPath::try_from("/svc/b").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let invalid_source_name_expose_from_child_decl = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child("my_child".to_string()),
            source_name: "c".into(),
            target: ExposeTarget::Parent,
            target_name: "c".into(),
        });

        let invalid_source_expose_from_child_decl = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Child("bad_child".to_string()),
            source_name: "d".into(),
            target: ExposeTarget::Parent,
            target_name: "d".into(),
        });

        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .use_(invalid_source_name_use_from_child_decl)
                    .use_(invalid_source_use_from_child_decl)
                    .expose(invalid_source_name_expose_from_child_decl)
                    .expose(invalid_source_expose_from_child_decl)
                    .add_lazy_child("my_child")
                    .build(),
            ),
            ("my_child", ComponentDeclBuilder::new().build()),
        ];

        let TestModelResult { model, builtin_environment, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let route_validator = {
            let env = builtin_environment.lock().await;
            env.route_validator.clone().unwrap()
        };

        let (validator, validator_request_stream) =
            create_proxy_and_stream::<fsys::RouteValidatorMarker>().unwrap();

        let _validator_task = fasync::Task::local(async move {
            route_validator.serve(AbsoluteMoniker::root(), validator_request_stream).await
        });

        model.start().await;

        // `my_child` should not be resolved right now
        let instance = model.find_resolved(&vec!["my_child"].into()).await;
        assert!(instance.is_none());

        // Validate the root
        let mut results = validator.validate(".").await.unwrap().unwrap();
        assert_eq!(results.len(), 4);

        results.sort_by_key(|r| Key {
            capability: r.capability.clone().unwrap(),
            decl_type: r.decl_type.clone().unwrap(),
        });

        let report = results.remove(0);
        assert_eq!(report.capability.unwrap(), "a");
        assert_eq!(report.decl_type.unwrap(), fsys::DeclType::Use);
        assert!(report.error.is_some());

        let report = results.remove(0);
        assert_eq!(report.capability.unwrap(), "b");
        assert_eq!(report.decl_type.unwrap(), fsys::DeclType::Use);
        assert!(report.error.is_some());

        let report = results.remove(0);
        assert_eq!(report.capability.unwrap(), "c");
        assert_eq!(report.decl_type.unwrap(), fsys::DeclType::Expose);
        assert!(report.error.is_some());

        let report = results.remove(0);
        assert_eq!(report.capability.unwrap(), "d");
        assert_eq!(report.decl_type.unwrap(), fsys::DeclType::Expose);
        assert!(report.error.is_some());

        // This validation should have caused `my_child` to be resolved
        let instance = model.find_resolved(&vec!["my_child"].into()).await;
        assert!(instance.is_some());
    }
}
