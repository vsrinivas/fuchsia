// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod admin_protocol;
use {
    crate::{
        channel,
        model::{
            component::{BindReason, ComponentInstance},
            error::ModelError,
        },
    },
    anyhow::Error,
    clonable_error::ClonableError,
    cm_rust::CapabilityPath,
    fidl::endpoints,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    moniker::{AbsoluteMoniker, ChildMonikerBase, RelativeMoniker, RelativeMonikerBase},
    routing::component_id_index::ComponentInstanceId,
    std::path::PathBuf,
    thiserror::Error,
};

const FLAGS: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

pub type StorageCapabilitySource =
    ::routing::capability_source::StorageCapabilitySource<ComponentInstance>;

/// Errors related to isolated storage.
#[derive(Debug, Error, Clone)]
pub enum StorageError {
    #[error(
        "failed to open isolated storage from {:?}'s directory {} for {} (instance_id={:?}): {} ",
        dir_source_moniker,
        dir_source_path,
        relative_moniker,
        instance_id,
        err
    )]
    Open {
        dir_source_moniker: Option<AbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        relative_moniker: RelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
        #[source]
        err: ClonableError,
    },
    #[error(
        "failed to remove isolated storage from {:?}'s directory {} for {} (instance_id={:?}): {} ",
        dir_source_moniker,
        dir_source_path,
        relative_moniker,
        instance_id,
        err
    )]
    Remove {
        dir_source_moniker: Option<AbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        relative_moniker: RelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
        #[source]
        err: ClonableError,
    },
    #[error(
        "storage path for relative_moniker={}, instance_id={:?} is invalid",
        relative_moniker,
        instance_id
    )]
    InvalidStoragePath {
        relative_moniker: RelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
    },
}

impl StorageError {
    pub fn open(
        dir_source_moniker: Option<AbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        relative_moniker: RelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
        err: impl Into<Error>,
    ) -> Self {
        Self::Open {
            dir_source_moniker,
            dir_source_path,
            relative_moniker,
            instance_id,
            err: err.into().into(),
        }
    }

    pub fn remove(
        dir_source_moniker: Option<AbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        relative_moniker: RelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
        err: impl Into<Error>,
    ) -> Self {
        Self::Remove {
            dir_source_moniker,
            dir_source_path,
            relative_moniker,
            instance_id,
            err: err.into().into(),
        }
    }

    pub fn invalid_storage_path(
        relative_moniker: RelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
    ) -> Self {
        Self::InvalidStoragePath { relative_moniker, instance_id }
    }
}

/// Open the isolated storage sub-directory for the given component, creating it if necessary.
/// `dir_source_component` and `dir_source_path` are the component hosting the directory and its
/// capability path.
///
/// The storage sub-directory is based on provided instance ID if present, otherwise it is based on
/// the provided relative moniker.
pub async fn open_isolated_storage(
    storage_source_info: StorageCapabilitySource,
    relative_moniker: RelativeMoniker,
    instance_id: Option<&ComponentInstanceId>,
    open_mode: u32,
    bind_reason: &BindReason,
) -> Result<DirectoryProxy, ModelError> {
    // TODO: The `use` declaration for storage implicitly carries these rights. While this is
    // correct, it would be more consistent to get the rights from `CapabilityState` and pass them
    // here.
    const FLAGS: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;
    let (mut dir_proxy, local_server_end) =
        endpoints::create_proxy::<DirectoryMarker>().expect("failed to create proxy");
    let mut local_server_end = local_server_end.into_channel();
    let full_backing_directory_path = match storage_source_info.backing_directory_subdir.as_ref() {
        Some(subdir) => storage_source_info.backing_directory_path.to_path_buf().join(subdir),
        None => storage_source_info.backing_directory_path.to_path_buf(),
    };
    if let Some(dir_source_component) = storage_source_info.storage_provider.as_ref() {
        dir_source_component
            .bind(bind_reason)
            .await?
            .open_outgoing(
                FLAGS,
                open_mode,
                full_backing_directory_path.clone(),
                &mut local_server_end,
            )
            .await?;
    } else {
        // If storage_source_info.storage_provider is None, the directory comes from component_manager's namespace
        let local_server_end = channel::take_channel(&mut local_server_end);
        let path = full_backing_directory_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(full_backing_directory_path.clone()))?;
        io_util::connect_in_namespace(path, local_server_end, FLAGS).map_err(|e| {
            ModelError::from(StorageError::open(
                None,
                storage_source_info.backing_directory_path.clone(),
                relative_moniker.clone(),
                instance_id.cloned(),
                e,
            ))
        })?;
    }
    if let Some(subdir) = storage_source_info.storage_subdir.as_ref() {
        dir_proxy = io_util::create_sub_directories(&dir_proxy, subdir.as_path()).map_err(|e| {
            ModelError::from(StorageError::open(
                storage_source_info.storage_provider.as_ref().map(|r| r.abs_moniker.clone()),
                storage_source_info.backing_directory_path.clone(),
                relative_moniker.clone(),
                instance_id.cloned(),
                e,
            ))
        })?;
    }
    let storage_path = instance_id
        .map(|id| generate_instance_id_based_storage_path(id))
        .unwrap_or_else(|| generate_moniker_based_storage_path(&relative_moniker));

    io_util::create_sub_directories(&dir_proxy, &storage_path).map_err(|e| {
        ModelError::from(StorageError::open(
            storage_source_info.storage_provider.as_ref().map(|r| r.abs_moniker.clone()),
            storage_source_info.backing_directory_path.clone(),
            relative_moniker.clone(),
            instance_id.cloned(),
            e,
        ))
    })
}

/// Delete the isolated storage sub-directory for the given component.  `dir_source_component` and
/// `dir_source_path` are the component hosting the directory and its capability path.
pub async fn delete_isolated_storage(
    storage_source_info: StorageCapabilitySource,
    relative_moniker: RelativeMoniker,
    instance_id: Option<&ComponentInstanceId>,
) -> Result<(), ModelError> {
    let (root_dir, local_server_end) =
        endpoints::create_proxy::<DirectoryMarker>().expect("failed to create proxy");
    let mut local_server_end = local_server_end.into_channel();
    let full_backing_directory_path = match storage_source_info.backing_directory_subdir.as_ref() {
        Some(subdir) => storage_source_info.backing_directory_path.to_path_buf().join(subdir),
        None => storage_source_info.backing_directory_path.to_path_buf(),
    };
    if let Some(dir_source_component) = storage_source_info.storage_provider.as_ref() {
        // TODO(fxbug.dev/50716): This BindReason is wrong. We need to refactor the Storage
        // capability to plumb through the correct BindReason.
        dir_source_component
            .bind(&BindReason::Unsupported)
            .await?
            .open_outgoing(
                FLAGS,
                MODE_TYPE_DIRECTORY,
                full_backing_directory_path,
                &mut local_server_end,
            )
            .await?;
    } else {
        let local_server_end = channel::take_channel(&mut local_server_end);
        let path = full_backing_directory_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(full_backing_directory_path.clone()))?;
        io_util::connect_in_namespace(path, local_server_end, FLAGS).map_err(|e| {
            StorageError::open(
                None,
                storage_source_info.backing_directory_path.clone(),
                relative_moniker.clone(),
                None,
                e,
            )
        })?;
    }

    let (dir, name) = if let Some(instance_id) = instance_id {
        let storage_path = generate_instance_id_based_storage_path(instance_id);
        let file_name = storage_path
            .file_name()
            .ok_or_else(|| {
                StorageError::invalid_storage_path(
                    relative_moniker.clone(),
                    Some(instance_id.clone()),
                )
            })?
            .to_str()
            .ok_or_else(|| ModelError::name_is_not_utf8(storage_path.clone().into_os_string()))?
            .to_string();

        let parent_path = storage_path.parent().ok_or_else(|| {
            StorageError::invalid_storage_path(relative_moniker.clone(), Some(instance_id.clone()))
        })?;
        let dir = if parent_path
            .to_str()
            .ok_or_else(|| ModelError::name_is_not_utf8(storage_path.clone().into_os_string()))?
            .is_empty()
        {
            root_dir
        } else {
            io_util::open_directory(&root_dir, parent_path, FLAGS).map_err(|e| {
                StorageError::open(
                    storage_source_info.storage_provider.as_ref().map(|r| r.abs_moniker.clone()),
                    storage_source_info.backing_directory_path.clone(),
                    relative_moniker.clone(),
                    None,
                    e,
                )
            })?
        };
        (dir, file_name)
    } else {
        let storage_path = generate_moniker_based_storage_path(&relative_moniker);
        // We want to strip off the "data" portion of the path, and then one more level to get to the
        // directory holding the target component's storage.
        if storage_path.parent().and_then(|p| p.parent()).is_none() {
            return Err(StorageError::invalid_storage_path(relative_moniker.clone(), None).into());
        }
        let mut dir_path = storage_path.parent().unwrap().parent().unwrap().to_path_buf();
        let name = storage_path.parent().unwrap().file_name().ok_or_else(|| {
            ModelError::from(StorageError::invalid_storage_path(relative_moniker.clone(), None))
        })?;
        let name =
            name.to_str().ok_or_else(|| ModelError::name_is_not_utf8(name.to_os_string()))?;
        if let Some(subdir) = storage_source_info.storage_subdir.as_ref() {
            dir_path = subdir.join(dir_path);
        }

        let dir = if dir_path.parent().is_none() {
            root_dir
        } else {
            io_util::open_directory(&root_dir, &dir_path, FLAGS).map_err(|e| {
                StorageError::open(
                    storage_source_info.storage_provider.as_ref().map(|r| r.abs_moniker.clone()),
                    storage_source_info.backing_directory_path.clone(),
                    relative_moniker.clone(),
                    None,
                    e,
                )
            })?
        };
        (dir, name.to_string())
    };

    // TODO(fxbug.dev/36377): This function is subject to races. If another process has a handle to the
    // isolated storage directory, it can add files while this function is running. That could
    // cause it to spin or fail because a subdir was not empty after it removed all the contents.
    // It's also possible that the directory was already deleted by the backing component or a
    // prior run.
    files_async::remove_dir_recursive(&dir, &name).await.map_err(|e| {
        StorageError::remove(
            storage_source_info.storage_provider.as_ref().map(|r| r.abs_moniker.clone()),
            storage_source_info.backing_directory_path.clone(),
            relative_moniker.clone(),
            instance_id.cloned(),
            e,
        )
    })?;
    Ok(())
}

/// Generates the path into a directory the provided component will be afforded for storage
///
/// The path of the sub-directory for a component that uses a storage capability is based on each
/// component instance's child moniker as given in the `children` section of its parent's manifest,
/// for each component instance in the path from the `storage` declaration to the
/// `use` declaration.
///
/// These names are used as path elements, separated by elements of the name "children". The
/// string "data" is then appended to this path for compatibility reasons.
///
/// For example, if the following component instance tree exists, with `a` declaring storage
/// capabilities, and then storage being offered down the chain to `d`:
///
/// ```
///  a  <- declares storage "cache", offers "cache" to b
///  |
///  b  <- offers "cache" to c
///  |
///  c  <- offers "cache" to d
///  |
///  d  <- uses "cache" storage as `/my_cache`
/// ```
///
/// When `d` attempts to access `/my_cache` the framework creates the sub-directory
/// `b:0/children/c:0/children/d:0/data` in the directory used by `a` to declare storage
/// capabilities.  Then, the framework gives 'd' access to this new directory.
fn generate_moniker_based_storage_path(relative_moniker: &RelativeMoniker) -> PathBuf {
    assert!(
        !relative_moniker.down_path().is_empty(),
        "storage capability appears to have been exposed or used by its source"
    );

    let mut down_path = relative_moniker.down_path().iter();
    let mut dir_path = vec![down_path.next().unwrap().as_str().to_string()];
    while let Some(p) = down_path.next() {
        dir_path.push("children".to_string());
        dir_path.push(p.as_str().to_string());
    }

    // Storage capabilities used to have a hardcoded set of types, which would be appended
    // here. To maintain compatibility with the old paths (and thus not lose data when this was
    // migrated) we append "data" here. This works because this is the only type of storage
    // that was actually used in the wild.
    //
    // This is only temporary, until the storage instance id migration changes this layout.
    dir_path.push("data".to_string());
    dir_path.into_iter().collect()
}

/// Generates the component storage directory path for the provided component instance.
///
/// Components which do not have an instance ID use a generate moniker-based storage path instead.
fn generate_instance_id_based_storage_path(instance_id: &ComponentInstanceId) -> PathBuf {
    [instance_id].iter().collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::model::{
            component::BindReason,
            rights,
            routing::error::OpenResourceError,
            testing::{
                routing_test_helpers::{RoutingTest, RoutingTestBuilder},
                test_helpers::{self, component_decl_with_test_runner},
            },
        },
        cm_rust::*,
        cm_rust_testing::ComponentDeclBuilder,
        component_id_index, fidl_fuchsia_io2 as fio2,
        matches::assert_matches,
        moniker::AbsoluteMonikerBase,
        rand::{self, distributions::Alphanumeric, Rng},
        std::{
            convert::{TryFrom, TryInto},
            sync::Arc,
        },
    };

    #[fuchsia::test]
    async fn open_isolated_storage_test() {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").add_lazy_child("c").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDecl {
                        name: "data".into(),
                        source_path: Some("/data".try_into().unwrap()),
                        rights: *rights::READ_RIGHTS | *rights::WRITE_RIGHTS,
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "data".try_into().unwrap(),
                        source: ExposeSource::Self_,
                        target_name: "data".try_into().unwrap(),
                        target: ExposeTarget::Parent,
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let test = RoutingTest::new("a", components).await;
        let b_component =
            test.model.look_up(&vec!["b"].into()).await.expect("failed to find component for b:0");
        let dir_source_path = CapabilityPath::try_from("/data").unwrap();
        let relative_moniker = RelativeMoniker::new(vec![], vec!["c:0".into(), "coll:d:1".into()]);

        // Open.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            relative_moniker.clone(),
            None,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");
        assert_eq!(test_helpers::list_directory(&dir).await, Vec::<String>::new());
        test_helpers::write_file(&dir, "file", "hippos").await;
        assert_eq!(test_helpers::list_directory(&dir).await, vec!["file".to_string()]);

        // Open again.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            relative_moniker.clone(),
            None,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");
        assert_eq!(test_helpers::list_directory(&dir).await, vec!["file".to_string()]);

        // Open another component's storage.
        let relative_moniker =
            RelativeMoniker::new(vec![], vec!["c:0".into(), "coll:d:1".into(), "e:0".into()]);
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            relative_moniker.clone(),
            None,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");
        assert_eq!(test_helpers::list_directory(&dir).await, Vec::<String>::new());
        test_helpers::write_file(&dir, "file", "hippos").await;
        assert_eq!(test_helpers::list_directory(&dir).await, vec!["file".to_string()]);
    }

    #[fuchsia::test]
    async fn open_isolated_storage_instance_id() {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").add_lazy_child("c").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDecl {
                        name: "data".into(),
                        source_path: Some("/data".try_into().unwrap()),
                        rights: *rights::READ_RIGHTS | *rights::WRITE_RIGHTS,
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "data".try_into().unwrap(),
                        source: ExposeSource::Self_,
                        target_name: "data".try_into().unwrap(),
                        target: ExposeTarget::Parent,
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let test = RoutingTest::new("a", components).await;
        let b_component =
            test.model.look_up(&vec!["b"].into()).await.expect("failed to find component for b:0");
        let dir_source_path = CapabilityPath::try_from("/data").unwrap();
        let relative_moniker = RelativeMoniker::new(vec![], vec!["c:0".into(), "coll:d:1".into()]);

        // open the storage directory using instance ID.
        let instance_id = Some(component_id_index::gen_instance_id(&mut rand::thread_rng()));
        let mut dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            relative_moniker.clone(),
            instance_id.as_ref(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");

        // ensure the directory is actually open before querying its parent about it.
        dir.describe().await.expect("failed to open directory");

        // check that an instance-ID based directory was created:
        assert!(test_helpers::list_directory(&test.test_dir_proxy)
            .await
            .contains(&instance_id.clone().unwrap()));

        // check that a moniker-based directory was NOT created:
        assert!(!test_helpers::list_directory_recursive(&test.test_dir_proxy).await.contains(
            &generate_moniker_based_storage_path(&relative_moniker).to_str().unwrap().to_string()
        ));

        // check that the directory is writable by writing a marker file in it.
        let marker_file_name: String =
            rand::thread_rng().sample_iter(&Alphanumeric).take(7).map(char::from).collect();
        assert_eq!(test_helpers::list_directory(&dir).await, Vec::<String>::new());
        test_helpers::write_file(&dir, &marker_file_name, "contents").await;
        assert_eq!(test_helpers::list_directory(&dir).await, vec![marker_file_name.clone()]);

        // check that re-opening the directory gives us the same marker file.
        dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            relative_moniker.clone(),
            instance_id.as_ref(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");

        assert_eq!(test_helpers::list_directory(&dir).await, vec![marker_file_name.clone()]);
    }

    // TODO: test with different subdirs

    #[fuchsia::test]
    async fn open_isolated_storage_failure_test() {
        let components = vec![("a", component_decl_with_test_runner())];

        // Create a universe with a single component, whose outgoing directory service
        // simply closes the channel of incoming requests.
        let test = RoutingTestBuilder::new("a", components)
            .set_component_outgoing_host_fn("a", Box::new(|_| {}))
            .build()
            .await;
        test.bind_instance_and_wait_start(&AbsoluteMoniker::root()).await.unwrap();

        // Try to open the storage. We expect an error.
        let relative_moniker = RelativeMoniker::new(vec![], vec!["c:0".into(), "coll:d:1".into()]);
        let res = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&test.model.root())),
                backing_directory_path: CapabilityPath::try_from("/data").unwrap().clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            relative_moniker.clone(),
            None,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await;
        assert_matches!(
            res,
            Err(ModelError::OpenResourceError {
                err: OpenResourceError::OpenOutgoingFailed { .. }
            })
        );
    }

    #[fuchsia::test]
    async fn delete_isolated_storage_test() {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").add_lazy_child("c").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDecl {
                        name: "data".into(),
                        source_path: Some("/data".try_into().unwrap()),
                        rights: *rights::READ_RIGHTS | *rights::WRITE_RIGHTS,
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "data".try_into().unwrap(),
                        source: ExposeSource::Self_,
                        target_name: "data".try_into().unwrap(),
                        target: ExposeTarget::Parent,
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let test = RoutingTest::new("a", components).await;
        let b_component =
            test.model.look_up(&vec!["b"].into()).await.expect("failed to find component for b:0");
        let dir_source_path = CapabilityPath::try_from("/data").unwrap();
        let parent_moniker = RelativeMoniker::new(vec![], vec!["c:0".into()]);
        let child_moniker = RelativeMoniker::new(vec![], vec!["c:0".into(), "coll:d:1".into()]);

        // Open and write to the storage for child.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            child_moniker.clone(),
            None,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");
        assert_eq!(test_helpers::list_directory(&dir).await, Vec::<String>::new());
        test_helpers::write_file(&dir, "file", "hippos").await;
        assert_eq!(test_helpers::list_directory(&dir).await, vec!["file".to_string()]);

        // Open parent's storage.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            parent_moniker.clone(),
            None,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");
        assert_eq!(test_helpers::list_directory(&dir).await, Vec::<String>::new());
        test_helpers::write_file(&dir, "file", "hippos").await;
        assert_eq!(test_helpers::list_directory(&dir).await, vec!["file".to_string()]);

        // Delete the child's storage.
        delete_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            child_moniker.clone(),
            None,
        )
        .await
        .expect("failed to delete child's isolated storage");

        // Open parent's storage again. Should work.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            parent_moniker.clone(),
            None,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");
        assert_eq!(test_helpers::list_directory(&dir).await, vec!["file".to_string()]);

        // Open list of children from parent. Should not contain child directory.
        assert_eq!(
            test.list_directory_in_storage(None, parent_moniker.clone(), None, "children").await,
            Vec::<String>::new(),
        );

        // Error -- tried to delete nonexistent storage.
        let err = delete_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path,
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            child_moniker,
            None,
        )
        .await
        .expect_err("delete isolated storage not meant to succeed");
        match err {
            ModelError::StorageError { err: StorageError::Remove { .. } } => {}
            _ => {
                panic!("unexpected error: {:?}", err);
            }
        }
    }

    #[fuchsia::test]
    async fn delete_isolated_storage_instance_id_test() {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").add_lazy_child("c").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDecl {
                        name: "data".into(),
                        source_path: Some("/data".try_into().unwrap()),
                        rights: *rights::READ_RIGHTS | *rights::WRITE_RIGHTS,
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "data".try_into().unwrap(),
                        source: ExposeSource::Self_,
                        target_name: "data".try_into().unwrap(),
                        target: ExposeTarget::Parent,
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let test = RoutingTest::new("a", components).await;
        let b_component =
            test.model.look_up(&vec!["b"].into()).await.expect("failed to find component for b:0");
        let dir_source_path = CapabilityPath::try_from("/data").unwrap();
        let child_moniker = RelativeMoniker::new(vec![], vec!["c:0".into(), "coll:d:1".into()]);
        let instance_id = Some(component_id_index::gen_instance_id(&mut rand::thread_rng()));
        // Open and write to the storage for child.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            child_moniker.clone(),
            instance_id.as_ref(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            &BindReason::Eager,
        )
        .await
        .expect("failed to open isolated storage");

        // ensure the directory is actually open before querying its parent about it.
        dir.describe().await.expect("failed to open directory");

        // check that an instance-ID based directory was created:
        assert!(test_helpers::list_directory(&test.test_dir_proxy)
            .await
            .contains(&instance_id.clone().unwrap()));

        assert_eq!(test_helpers::list_directory(&dir).await, Vec::<String>::new());
        test_helpers::write_file(&dir, "file", "hippos").await;
        assert_eq!(test_helpers::list_directory(&dir).await, vec!["file".to_string()]);

        // Delete the child's storage.
        delete_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            child_moniker.clone(),
            instance_id.as_ref(),
        )
        .await
        .expect("failed to delete child's isolated storage");

        // check that an instance-ID based directory was deleted:
        assert!(!test_helpers::list_directory(&test.test_dir_proxy)
            .await
            .contains(&instance_id.clone().unwrap()));

        // Error -- tried to delete nonexistent storage.
        let err = delete_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path,
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            child_moniker,
            instance_id.as_ref(),
        )
        .await
        .expect_err("delete isolated storage not meant to succeed");
        match err {
            ModelError::StorageError { err: StorageError::Remove { .. } } => {}
            _ => {
                panic!("unexpected error: {:?}", err);
            }
        }
    }

    #[test]
    fn generate_moniker_based_storage_path_test() {
        for (relative_moniker, expected_output) in vec![
            (
                RelativeMoniker::from_absolute(
                    &AbsoluteMoniker::from(vec![]),
                    &AbsoluteMoniker::from(vec!["a:1"]),
                ),
                "a:1/data",
            ),
            (
                RelativeMoniker::from_absolute(
                    &AbsoluteMoniker::from(vec![]),
                    &AbsoluteMoniker::from(vec!["a:1", "b:2"]),
                ),
                "a:1/children/b:2/data",
            ),
            (
                RelativeMoniker::from_absolute(
                    &AbsoluteMoniker::from(vec![]),
                    &AbsoluteMoniker::from(vec!["a:1", "b:2", "c:3"]),
                ),
                "a:1/children/b:2/children/c:3/data",
            ),
        ] {
            assert_eq!(
                generate_moniker_based_storage_path(&relative_moniker),
                PathBuf::from(expected_output)
            )
        }
    }
}
