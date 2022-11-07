// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    ffx_component::verify_fuchsia_pkg_cm_url,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase},
};

pub static LIFECYCLE_ERROR_HELP: &'static str = "To learn more, see \
https://fuchsia.dev/go/components/run-errors";

pub static MONIKER_ERROR_HELP: &'static str = "Provide a moniker to a (not currently existing) \
component instance in a collection. To learn more, see \
https://fuchsia.dev/go/components/collections";

// Determines what to do if create finds that the component already exists.
pub enum IfExists {
    Recreate,      // Destroy, then recreate the component.
    Error(String), // Report the given message.
}

// Details of the current component if it already exists when a creation attempt is made.
struct CurrentComponentInfo {
    collection: Option<String>,
    parent: AbsoluteMoniker,
    name: String,
}

// Create a child with the given moniker. If the creation fails because the component already
// exists, return the information necessary to destroy it.
async fn create_child_component<W: std::io::Write>(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    moniker: &AbsoluteMoniker,
    url: &str,
    writer: &mut W,
) -> Result<Option<CurrentComponentInfo>> {
    verify_fuchsia_pkg_cm_url(url)?;

    let parent = moniker
        .parent()
        .ok_or(ffx_error!("Component moniker cannot be the root. {}", MONIKER_ERROR_HELP))?;
    let leaf = moniker
        .leaf()
        .ok_or(ffx_error!("Component moniker cannot be the root. {}", MONIKER_ERROR_HELP))?;
    let collection = leaf
        .collection()
        .ok_or(ffx_error!("Moniker references a static component. {}", MONIKER_ERROR_HELP))?;
    let name = leaf.name();

    writeln!(writer, "URL: {}", url)?;
    writeln!(writer, "Moniker: {}", moniker)?;
    writeln!(writer, "Creating component instance...")?;

    let mut collection_ref = fdecl::CollectionRef { name: collection.to_string() };
    let decl = fdecl::Child {
        name: Some(name.to_string()),
        url: Some(url.to_string()),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };
    // LifecycleController accepts RelativeMonikers only.
    let parent_relative_moniker = format!(".{}", parent.to_string());

    let result = lifecycle_controller
        .create_child(
            &parent_relative_moniker,
            &mut collection_ref,
            decl,
            fcomponent::CreateChildArgs::EMPTY,
        )
        .await
        .map_err(|e| ffx_error!("FIDL error while creating component instance: {:?}", e))?;

    match result {
        Err(fcomponent::Error::InstanceAlreadyExists) => Ok(Some(CurrentComponentInfo {
            collection: Some(collection.to_string()),
            parent: parent.clone(),
            name: name.to_string(),
        })),
        Err(e) => {
            ffx_bail!(
                "Lifecycle protocol could not create component instance: {:?}.\n{}",
                e,
                LIFECYCLE_ERROR_HELP
            );
        }
        Ok(()) => Ok(None),
    }
}

async fn destroy_child_component<W: std::io::Write>(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    collection: Option<String>,
    parent: &AbsoluteMoniker,
    name: String,
    writer: &mut W,
) -> Result<()> {
    let mut child = fdecl::ChildRef { name, collection };

    writeln!(writer, "Component instance already exists. Destroying...")?;
    // LifecycleController accepts RelativeMonikers only.
    let parent_relative_moniker_str = format!(".{}", parent.to_string());
    let destroy_result = lifecycle_controller
        .destroy_child(&parent_relative_moniker_str, &mut child)
        .await
        .map_err(|e| ffx_error!("FIDL error while destroying component instance: {:?}", e))?;

    if let Err(e) = destroy_result {
        ffx_bail!("Lifecycle protocol could not destroy component instance: {:?}", e);
    }
    Ok(())
}

// Create a new component with the given package as a child of the given moniker. If the child
// already exists then either destroy then recreate the child if `force` is true, or print the given
// message if `force` is false.
pub async fn create_component<W: std::io::Write>(
    lifecycle_controller: &fsys::LifecycleControllerProxy,
    moniker: &AbsoluteMoniker,
    url: &str,
    if_exists: IfExists,
    writer: &mut W,
) -> Result<()> {
    let res = create_child_component(&lifecycle_controller, moniker, url, writer).await?;

    match (res, if_exists) {
        (Some(info), IfExists::Recreate) => {
            writeln!(writer, "Component instance already exists. Destroying...")?;
            destroy_child_component(
                &lifecycle_controller,
                info.collection,
                &info.parent,
                info.name,
                writer,
            )
            .await?;
            writeln!(writer, "Recreating component instance...")?;
            let res = create_child_component(&lifecycle_controller, moniker, url, writer).await?;
            if res.is_some() {
                ffx_bail!("Failed to destroy the existing component")
            }
        }
        (Some(_), IfExists::Error(msg)) => {
            ffx_bail!("Component instance already exists. {}", msg);
        }
        (None, _) => {}
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use {
        crate::IfExists,
        anyhow::Result,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_sys2 as fsys,
        futures::TryStreamExt,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    };

    fn setup_fake_lifecycle_controller(
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_success() -> Result<()> {
        let mut output = Vec::new();
        let lifecycle_controller = setup_fake_lifecycle_controller(
            "./core",
            "ffx-laboratory",
            "test",
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
        );
        let response = crate::create_component(
            &lifecycle_controller,
            &AbsoluteMoniker::parse_str("/core/ffx-laboratory:test")?,
            "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
            IfExists::Error("".to_string()),
            &mut output,
        )
        .await;
        response.unwrap();
        Ok(())
    }
}
