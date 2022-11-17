// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::AbsoluteComponentUrl,
    moniker::{ChildMonikerBase, RelativeMoniker, RelativeMonikerBase},
    thiserror::Error,
};

#[derive(Error, Debug)]
pub enum LifecycleError {
    #[error("{moniker} is not an instance in a collection")]
    ExpectedDynamicInstance { moniker: RelativeMoniker },
    #[error("{moniker} already exists")]
    InstanceAlreadyExists { moniker: RelativeMoniker },
    #[error("{moniker} does not exist")]
    InstanceNotFound { moniker: RelativeMoniker },
    #[error("internal error in LifecycleController: {0:?}")]
    Internal(fcomponent::Error),
    #[error("unexpected FIDL error with LifecycleController")]
    Fidl(#[from] fidl::Error),
}

/// Uses the `fuchsia.sys2.LifecycleController` protocol to create a dynamic component instance
/// with the given `moniker` and `url`.
///
/// The moniker must reference an instance in a collection, otherwise an error is thrown.
pub async fn create_instance_in_collection(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    moniker: &RelativeMoniker,
    url: &AbsoluteComponentUrl,
) -> Result<(), LifecycleError> {
    let parent = moniker
        .parent()
        .ok_or(LifecycleError::ExpectedDynamicInstance { moniker: moniker.clone() })?;
    let leaf = moniker
        .leaf()
        .ok_or(LifecycleError::ExpectedDynamicInstance { moniker: moniker.clone() })?;
    let collection = leaf
        .collection()
        .ok_or(LifecycleError::ExpectedDynamicInstance { moniker: moniker.clone() })?;
    let name = leaf.name();

    let mut collection = fdecl::CollectionRef { name: collection.to_string() };
    let decl = fdecl::Child {
        name: Some(name.to_string()),
        url: Some(url.to_string()),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };

    let result = lifecycle_controller
        .create_child(
            &parent.to_string(),
            &mut collection,
            decl,
            fcomponent::CreateChildArgs::EMPTY,
        )
        .await?;

    match result {
        Err(fcomponent::Error::InstanceAlreadyExists) => {
            Err(LifecycleError::InstanceAlreadyExists { moniker: moniker.clone() })
        }
        Err(e) => Err(LifecycleError::Internal(e)),
        Ok(()) => Ok(()),
    }
}

/// Uses the `fuchsia.sys2.LifecycleController` protocol to destroy a dynamic component instance
/// with the given `moniker`.
///
/// The moniker must reference an instance in a collection, otherwise an error is thrown.
pub async fn destroy_instance_in_collection(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    moniker: &RelativeMoniker,
) -> Result<(), LifecycleError> {
    let parent = moniker
        .parent()
        .ok_or(LifecycleError::ExpectedDynamicInstance { moniker: moniker.clone() })?;
    let leaf = moniker
        .leaf()
        .ok_or(LifecycleError::ExpectedDynamicInstance { moniker: moniker.clone() })?;
    let collection = leaf
        .collection()
        .ok_or(LifecycleError::ExpectedDynamicInstance { moniker: moniker.clone() })?;
    let name = leaf.name();

    let mut child =
        fdecl::ChildRef { name: name.to_string(), collection: Some(collection.to_string()) };

    let result = lifecycle_controller.destroy_child(&parent.to_string(), &mut child).await?;

    match result {
        Err(fcomponent::Error::InstanceNotFound) => {
            Err(LifecycleError::InstanceNotFound { moniker: moniker.clone() })
        }
        Err(e) => Err(LifecycleError::Internal(e)),
        Ok(()) => Ok(()),
    }
}

/// Uses the `fuchsia.sys2.LifecycleController` protocol to start a component instance
/// with the given `moniker`.
///
/// Returns the result of the operation: the component was started or was already running.
pub async fn start_instance(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    moniker: &RelativeMoniker,
) -> Result<fsys::StartResult, LifecycleError> {
    match lifecycle_controller.start(&moniker.to_string()).await? {
        Ok(result) => Ok(result),
        Err(fcomponent::Error::InstanceNotFound) => {
            Err(LifecycleError::InstanceNotFound { moniker: moniker.clone() })
        }
        Err(e) => Err(LifecycleError::Internal(e)),
    }
}

/// Uses the `fuchsia.sys2.LifecycleController` protocol to stop a component instance
/// with the given `moniker`.
pub async fn stop_instance(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    moniker: &RelativeMoniker,
    recursive: bool,
) -> Result<(), LifecycleError> {
    match lifecycle_controller.stop(&moniker.to_string(), recursive).await? {
        Ok(()) => Ok(()),
        Err(fcomponent::Error::InstanceNotFound) => {
            Err(LifecycleError::InstanceNotFound { moniker: moniker.clone() })
        }
        Err(e) => Err(LifecycleError::Internal(e)),
    }
}

/// Uses the `fuchsia.sys2.LifecycleController` protocol to resolve a component instance
/// with the given `moniker`.
pub async fn resolve_instance(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    moniker: &RelativeMoniker,
) -> Result<(), LifecycleError> {
    match lifecycle_controller.resolve(&moniker.to_string()).await? {
        Ok(()) => Ok(()),
        Err(fcomponent::Error::InstanceNotFound) => {
            Err(LifecycleError::InstanceNotFound { moniker: moniker.clone() })
        }
        Err(e) => Err(LifecycleError::Internal(e)),
    }
}

/// Uses the `fuchsia.sys2.LifecycleController` protocol to unresolve a component instance
/// with the given `moniker`.
pub async fn unresolve_instance(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    moniker: &RelativeMoniker,
) -> Result<(), LifecycleError> {
    match lifecycle_controller.unresolve(&moniker.to_string()).await? {
        Ok(()) => Ok(()),
        Err(fcomponent::Error::InstanceNotFound) => {
            Err(LifecycleError::InstanceNotFound { moniker: moniker.clone() })
        }
        Err(e) => Err(LifecycleError::Internal(e)),
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, assert_matches::assert_matches, fidl::endpoints::create_proxy_and_stream,
        futures::TryStreamExt,
    };

    fn lifecycle_create_child(
        expected_moniker: &'static str,
        expected_collection: &'static str,
        expected_name: &'static str,
        expected_url: &'static str,
    ) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::CreateChild {
                    parent_moniker,
                    collection,
                    decl,
                    responder,
                    ..
                } => {
                    assert_eq!(expected_moniker, parent_moniker);
                    assert_eq!(expected_collection, collection.name);
                    assert_eq!(expected_name, decl.name.unwrap());
                    assert_eq!(expected_url, decl.url.unwrap());
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    fn lifecycle_destroy_child(
        expected_moniker: &'static str,
        expected_collection: &'static str,
        expected_name: &'static str,
    ) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::DestroyChild {
                    parent_moniker,
                    child,
                    responder,
                    ..
                } => {
                    assert_eq!(expected_moniker, parent_moniker);
                    assert_eq!(expected_name, child.name);
                    assert_eq!(expected_collection, child.collection.unwrap());
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    fn lifecycle_start(expected_moniker: &'static str) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Start { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    responder.send(&mut Ok(fsys::StartResult::Started)).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    fn lifecycle_stop(expected_moniker: &'static str) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Stop { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    fn lifecycle_resolve(expected_moniker: &'static str) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Resolve { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    fn lifecycle_unresolve(expected_moniker: &'static str) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Unresolve { moniker, responder, .. } => {
                    assert_eq!(expected_moniker, moniker);
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    fn lifecycle_fail(error: fcomponent::Error) -> fsys::LifecycleControllerProxy {
        let (lifecycle_controller, mut stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let req = stream.try_next().await.unwrap().unwrap();
            match req {
                fsys::LifecycleControllerRequest::Unresolve { responder, .. } => {
                    responder.send(&mut Err(error)).unwrap();
                }
                fsys::LifecycleControllerRequest::Resolve { responder, .. } => {
                    responder.send(&mut Err(error)).unwrap();
                }
                fsys::LifecycleControllerRequest::Start { responder, .. } => {
                    responder.send(&mut Err(error)).unwrap();
                }
                fsys::LifecycleControllerRequest::Stop { responder, .. } => {
                    responder.send(&mut Err(error)).unwrap();
                }
                fsys::LifecycleControllerRequest::CreateChild { responder, .. } => {
                    responder.send(&mut Err(error)).unwrap();
                }
                fsys::LifecycleControllerRequest::DestroyChild { responder, .. } => {
                    responder.send(&mut Err(error)).unwrap();
                }
                _ => panic!("Unexpected Lifecycle Controller request"),
            }
        })
        .detach();
        lifecycle_controller
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_child() {
        let moniker = RelativeMoniker::parse_str("./core/foo:bar").unwrap();
        let url =
            AbsoluteComponentUrl::parse("fuchsia-pkg://fuchsia.com/test#meta/test.cm").unwrap();
        let lc = lifecycle_create_child(
            "./core",
            "foo",
            "bar",
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
        );
        create_instance_in_collection(&lc, &moniker, &url).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_not_dynamic_instance() {
        let moniker = RelativeMoniker::parse_str("./core/foo").unwrap();
        let url =
            AbsoluteComponentUrl::parse("fuchsia-pkg://fuchsia.com/test#meta/test.cm").unwrap();
        let (lc, _stream) = create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        let err = create_instance_in_collection(&lc, &moniker, &url).await.unwrap_err();
        assert_matches!(err, LifecycleError::ExpectedDynamicInstance { .. });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_already_exists() {
        let moniker = RelativeMoniker::parse_str("./core/foo:bar").unwrap();
        let url =
            AbsoluteComponentUrl::parse("fuchsia-pkg://fuchsia.com/test#meta/test.cm").unwrap();
        let lc = lifecycle_fail(fcomponent::Error::InstanceAlreadyExists);
        let err = create_instance_in_collection(&lc, &moniker, &url).await.unwrap_err();
        assert_matches!(err, LifecycleError::InstanceAlreadyExists { .. });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_destroy_child() {
        let moniker = RelativeMoniker::parse_str("./core/foo:bar").unwrap();
        let lc = lifecycle_destroy_child("./core", "foo", "bar");
        destroy_instance_in_collection(&lc, &moniker).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_destroy_not_dynamic_instance() {
        let moniker = RelativeMoniker::parse_str("./core/foo").unwrap();
        let (lc, _stream) = create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();
        let err = destroy_instance_in_collection(&lc, &moniker).await.unwrap_err();
        assert_matches!(err, LifecycleError::ExpectedDynamicInstance { .. });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start() {
        let moniker = RelativeMoniker::parse_str("./core/foo").unwrap();
        let lc = lifecycle_start("./core/foo");
        let result = start_instance(&lc, &moniker).await.unwrap();
        assert_eq!(result, fsys::StartResult::Started);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stop() {
        let moniker = RelativeMoniker::parse_str("./core/foo").unwrap();
        let lc = lifecycle_stop("./core/foo");
        stop_instance(&lc, &moniker, false).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_resolve() {
        let moniker = RelativeMoniker::parse_str("./core/foo").unwrap();
        let lc = lifecycle_resolve("./core/foo");
        resolve_instance(&lc, &moniker).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_unresolve() {
        let moniker = RelativeMoniker::parse_str("./core/foo").unwrap();
        let lc = lifecycle_unresolve("./core/foo");
        unresolve_instance(&lc, &moniker).await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_instance_not_found() {
        let moniker = RelativeMoniker::parse_str("./core/foo").unwrap();
        let lc = lifecycle_fail(fcomponent::Error::InstanceNotFound);
        let err = start_instance(&lc, &moniker).await.unwrap_err();
        assert_matches!(err, LifecycleError::InstanceNotFound { .. });
    }
}
