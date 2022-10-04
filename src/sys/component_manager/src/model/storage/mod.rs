// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod admin_protocol;
use {
    crate::model::{
        component::{ComponentInstance, StartReason},
        error::ModelError,
    },
    anyhow::Error,
    clonable_error::ClonableError,
    cm_moniker::{InstancedAbsoluteMoniker, InstancedRelativeMoniker},
    cm_rust::CapabilityPath,
    fidl::endpoints,
    fidl_fuchsia_io as fio,
    moniker::{ChildMonikerBase, RelativeMonikerBase},
    routing::{
        component_id_index::ComponentInstanceId, component_instance::ComponentInstanceInterface,
    },
    std::path::PathBuf,
    thiserror::Error,
};

// TODO: The `use` declaration for storage implicitly carries these rights. While this is
// correct, it would be more consistent to get the rights from `CapabilityState`.
const FLAGS: fio::OpenFlags = fio::OpenFlags::empty()
    .union(fio::OpenFlags::RIGHT_READABLE)
    .union(fio::OpenFlags::RIGHT_WRITABLE);

pub type StorageCapabilitySource =
    ::routing::capability_source::StorageCapabilitySource<ComponentInstance>;

/// Errors related to isolated storage.
#[derive(Debug, Error, Clone)]
pub enum StorageError {
    #[error("failed to open {:?}'s directory {}: {} ", dir_source_moniker, dir_source_path, err)]
    OpenRoot {
        dir_source_moniker: Option<InstancedAbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        #[source]
        err: ClonableError,
    },
    #[error(
        "failed to open isolated storage from {:?}'s directory {} for {} (instance_id={:?}): {} ",
        dir_source_moniker,
        dir_source_path,
        relative_moniker,
        instance_id,
        err
    )]
    Open {
        dir_source_moniker: Option<InstancedAbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        relative_moniker: InstancedRelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
        #[source]
        err: ClonableError,
    },
    #[error(
        "failed to open isolated storage from {:?}'s directory {} for {:?}: {} ",
        dir_source_moniker,
        dir_source_path,
        instance_id,
        err
    )]
    OpenById {
        dir_source_moniker: Option<InstancedAbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        instance_id: ComponentInstanceId,
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
        dir_source_moniker: Option<InstancedAbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        relative_moniker: InstancedRelativeMoniker,
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
        relative_moniker: InstancedRelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
    },
}

impl StorageError {
    pub fn open_root(
        dir_source_moniker: Option<InstancedAbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        err: impl Into<Error>,
    ) -> Self {
        Self::OpenRoot { dir_source_moniker, dir_source_path, err: err.into().into() }
    }

    pub fn open(
        dir_source_moniker: Option<InstancedAbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        relative_moniker: InstancedRelativeMoniker,
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

    pub fn open_by_id(
        dir_source_moniker: Option<InstancedAbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        instance_id: ComponentInstanceId,
        err: impl Into<Error>,
    ) -> Self {
        Self::OpenById { dir_source_moniker, dir_source_path, instance_id, err: err.into().into() }
    }

    pub fn remove(
        dir_source_moniker: Option<InstancedAbsoluteMoniker>,
        dir_source_path: CapabilityPath,
        relative_moniker: InstancedRelativeMoniker,
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
        relative_moniker: InstancedRelativeMoniker,
        instance_id: Option<ComponentInstanceId>,
    ) -> Self {
        Self::InvalidStoragePath { relative_moniker, instance_id }
    }
}

async fn open_storage_root(
    storage_source_info: &StorageCapabilitySource,
) -> Result<fio::DirectoryProxy, ModelError> {
    let (mut dir_proxy, local_server_end) =
        endpoints::create_proxy::<fio::DirectoryMarker>().expect("failed to create proxy");
    let full_backing_directory_path = match storage_source_info.backing_directory_subdir.as_ref() {
        Some(subdir) => storage_source_info.backing_directory_path.to_path_buf().join(subdir),
        None => storage_source_info.backing_directory_path.to_path_buf(),
    };
    if let Some(dir_source_component) = storage_source_info.storage_provider.as_ref() {
        // TODO(fxbug.dev/50716): This should be StartReason::AccessCapability, but we haven't
        // plumbed in all the details needed to use it.
        dir_source_component.start(&StartReason::StorageAdmin).await?;
        dir_source_component
            .open_outgoing(
                FLAGS,
                fio::MODE_TYPE_DIRECTORY,
                full_backing_directory_path.clone(),
                &mut local_server_end.into_channel(),
            )
            .await?;
    } else {
        // If storage_source_info.storage_provider is None, the directory comes from component_manager's namespace
        let path = full_backing_directory_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(full_backing_directory_path.clone()))?;
        fuchsia_fs::directory::open_channel_in_namespace(path, FLAGS, local_server_end).map_err(
            |e| {
                ModelError::from(StorageError::open_root(
                    None,
                    storage_source_info.backing_directory_path.clone(),
                    e,
                ))
            },
        )?;
    }
    if let Some(subdir) = storage_source_info.storage_subdir.as_ref() {
        dir_proxy = fuchsia_fs::directory::create_directory_recursive(
            &dir_proxy,
            subdir.to_str().ok_or(ModelError::path_is_not_utf8(subdir.clone()))?,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .map_err(|e| {
            ModelError::from(StorageError::open_root(
                storage_source_info
                    .storage_provider
                    .as_ref()
                    .map(|r| r.instanced_moniker().clone()),
                storage_source_info.backing_directory_path.clone(),
                e,
            ))
        })?;
    }
    Ok(dir_proxy)
}

/// Open the isolated storage sub-directory from the given storage capability source, creating it
/// if necessary. The storage sub-directory is based on provided instance ID if present, otherwise
/// it is based on the provided relative moniker.
pub async fn open_isolated_storage(
    storage_source_info: StorageCapabilitySource,
    persistent_storage: bool,
    relative_moniker: InstancedRelativeMoniker,
    instance_id: Option<&ComponentInstanceId>,
) -> Result<fio::DirectoryProxy, ModelError> {
    let root_dir = open_storage_root(&storage_source_info).await?;
    let storage_path = match instance_id {
        Some(id) => generate_instance_id_based_storage_path(id),
        // if persistent_storage is `true`, generate a moniker-based storage path that ignores
        // instance ids.
        None => {
            if persistent_storage {
                generate_moniker_based_storage_path(
                    &relative_moniker.with_zero_value_instance_ids(),
                )
            } else {
                generate_moniker_based_storage_path(&relative_moniker)
            }
        }
    };

    fuchsia_fs::directory::create_directory_recursive(
        &root_dir,
        storage_path.to_str().ok_or(ModelError::path_is_not_utf8(storage_path.clone()))?,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .await
    .map_err(|e| {
        ModelError::from(StorageError::open(
            storage_source_info.storage_provider.as_ref().map(|r| r.instanced_moniker().clone()),
            storage_source_info.backing_directory_path.clone(),
            relative_moniker.clone(),
            instance_id.cloned(),
            e,
        ))
    })
}

/// Open the isolated storage sub-directory from the given storage capability source, creating it
/// if necessary. The storage sub-directory is based on provided instance ID.
pub async fn open_isolated_storage_by_id(
    storage_source_info: StorageCapabilitySource,
    instance_id: ComponentInstanceId,
) -> Result<fio::DirectoryProxy, ModelError> {
    let root_dir = open_storage_root(&storage_source_info).await?;
    let storage_path = generate_instance_id_based_storage_path(&instance_id);

    fuchsia_fs::directory::create_directory_recursive(
        &root_dir,
        storage_path.to_str().ok_or(ModelError::path_is_not_utf8(storage_path.clone()))?,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .await
    .map_err(|e| {
        ModelError::from(StorageError::open_by_id(
            storage_source_info.storage_provider.as_ref().map(|r| r.instanced_moniker().clone()),
            storage_source_info.backing_directory_path.clone(),
            instance_id,
            e,
        ))
    })
}

/// Delete the isolated storage sub-directory for the given component.  `dir_source_component` and
/// `dir_source_path` are the component hosting the directory and its capability path.
pub async fn delete_isolated_storage(
    storage_source_info: StorageCapabilitySource,
    persistent_storage: bool,
    relative_moniker: InstancedRelativeMoniker,
    instance_id: Option<&ComponentInstanceId>,
) -> Result<(), ModelError> {
    let root_dir = open_storage_root(&storage_source_info).await?;

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
            fuchsia_fs::open_directory(&root_dir, parent_path, FLAGS).map_err(|e| {
                StorageError::open(
                    storage_source_info
                        .storage_provider
                        .as_ref()
                        .map(|r| r.instanced_moniker().clone()),
                    storage_source_info.backing_directory_path.clone(),
                    relative_moniker.clone(),
                    None,
                    e,
                )
            })?
        };
        (dir, file_name)
    } else {
        let storage_path = if persistent_storage {
            generate_moniker_based_storage_path(&relative_moniker.with_zero_value_instance_ids())
        } else {
            generate_moniker_based_storage_path(&relative_moniker)
        };
        // We want to strip off the "data" portion of the path, and then one more level to get to the
        // directory holding the target component's storage.
        if storage_path.parent().and_then(|p| p.parent()).is_none() {
            return Err(StorageError::invalid_storage_path(relative_moniker.clone(), None).into());
        }
        let dir_path = storage_path.parent().unwrap().parent().unwrap().to_path_buf();
        let name = storage_path.parent().unwrap().file_name().ok_or_else(|| {
            ModelError::from(StorageError::invalid_storage_path(relative_moniker.clone(), None))
        })?;
        let name =
            name.to_str().ok_or_else(|| ModelError::name_is_not_utf8(name.to_os_string()))?;

        let dir = if dir_path.parent().is_none() {
            root_dir
        } else {
            fuchsia_fs::open_directory(&root_dir, &dir_path, FLAGS).map_err(|e| {
                StorageError::open(
                    storage_source_info
                        .storage_provider
                        .as_ref()
                        .map(|r| r.instanced_moniker().clone()),
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
    fuchsia_fs::directory::remove_dir_recursive(&dir, &name).await.map_err(|e| {
        StorageError::remove(
            storage_source_info.storage_provider.as_ref().map(|r| r.instanced_moniker().clone()),
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
fn generate_moniker_based_storage_path(relative_moniker: &InstancedRelativeMoniker) -> PathBuf {
    assert!(
        !relative_moniker.path().is_empty(),
        "storage capability appears to have been exposed or used by its source"
    );

    let mut path = relative_moniker.path().iter();
    let mut dir_path = vec![path.next().unwrap().as_str().to_string()];
    while let Some(p) = path.next() {
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
            routing::error::OpenResourceError,
            testing::{
                routing_test_helpers::{RoutingTest, RoutingTestBuilder},
                test_helpers::{self, component_decl_with_test_runner},
            },
        },
        assert_matches::assert_matches,
        cm_rust::*,
        cm_rust_testing::ComponentDeclBuilder,
        component_id_index, fidl_fuchsia_io as fio,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
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
                        rights: *routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS,
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "data".try_into().unwrap(),
                        source: ExposeSource::Self_,
                        target_name: "data".try_into().unwrap(),
                        target: ExposeTarget::Parent,
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let test = RoutingTest::new("a", components).await;
        let b_component =
            test.model.look_up(&vec!["b"].into()).await.expect("failed to find component for b:0");
        let dir_source_path = CapabilityPath::try_from("/data").unwrap();
        let relative_moniker = InstancedRelativeMoniker::new(vec!["c:0".into(), "coll:d:1".into()]);

        // Open.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            false,
            relative_moniker.clone(),
            None,
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
            false,
            relative_moniker.clone(),
            None,
        )
        .await
        .expect("failed to open isolated storage");
        assert_eq!(test_helpers::list_directory(&dir).await, vec!["file".to_string()]);

        // Open another component's storage.
        let relative_moniker =
            InstancedRelativeMoniker::new(vec!["c:0".into(), "coll:d:1".into(), "e:0".into()]);
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            false,
            relative_moniker.clone(),
            None,
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
                        rights: *routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS,
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "data".try_into().unwrap(),
                        source: ExposeSource::Self_,
                        target_name: "data".try_into().unwrap(),
                        target: ExposeTarget::Parent,
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let test = RoutingTest::new("a", components).await;
        let b_component =
            test.model.look_up(&vec!["b"].into()).await.expect("failed to find component for b:0");
        let dir_source_path = CapabilityPath::try_from("/data").unwrap();
        let relative_moniker = InstancedRelativeMoniker::new(vec!["c:0".into(), "coll:d:1".into()]);

        // open the storage directory using instance ID.
        let instance_id = Some(component_id_index::gen_instance_id(&mut rand::thread_rng()));
        let mut dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            false,
            relative_moniker.clone(),
            instance_id.as_ref(),
        )
        .await
        .expect("failed to open isolated storage");

        // ensure the directory is actually open before querying its parent about it.
        dir.describe_deprecated().await.expect("failed to open directory");

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
            false,
            relative_moniker.clone(),
            instance_id.as_ref(),
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
        test.start_instance_and_wait_start(&AbsoluteMoniker::root()).await.unwrap();

        // Try to open the storage. We expect an error.
        let relative_moniker = InstancedRelativeMoniker::new(vec!["c:0".into(), "coll:d:1".into()]);
        let res = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&test.model.root())),
                backing_directory_path: CapabilityPath::try_from("/data").unwrap().clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            false,
            relative_moniker.clone(),
            None,
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
                        rights: *routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS,
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "data".try_into().unwrap(),
                        source: ExposeSource::Self_,
                        target_name: "data".try_into().unwrap(),
                        target: ExposeTarget::Parent,
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let test = RoutingTest::new("a", components).await;
        let b_component =
            test.model.look_up(&vec!["b"].into()).await.expect("failed to find component for b:0");
        let dir_source_path = CapabilityPath::try_from("/data").unwrap();
        let parent_moniker = InstancedRelativeMoniker::new(vec!["c:0".into()]);
        let child_moniker = InstancedRelativeMoniker::new(vec!["c:0".into(), "coll:d:1".into()]);

        // Open and write to the storage for child.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            false,
            child_moniker.clone(),
            None,
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
            false,
            parent_moniker.clone(),
            None,
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
            false,
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
            false,
            parent_moniker.clone(),
            None,
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
            false,
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
                        rights: *routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS,
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "data".try_into().unwrap(),
                        source: ExposeSource::Self_,
                        target_name: "data".try_into().unwrap(),
                        target: ExposeTarget::Parent,
                        rights: Some(fio::Operations::CONNECT),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let test = RoutingTest::new("a", components).await;
        let b_component =
            test.model.look_up(&vec!["b"].into()).await.expect("failed to find component for b:0");
        let dir_source_path = CapabilityPath::try_from("/data").unwrap();
        let child_moniker = InstancedRelativeMoniker::new(vec!["c:0".into(), "coll:d:1".into()]);
        let instance_id = Some(component_id_index::gen_instance_id(&mut rand::thread_rng()));
        // Open and write to the storage for child.
        let dir = open_isolated_storage(
            StorageCapabilitySource {
                storage_provider: Some(Arc::clone(&b_component)),
                backing_directory_path: dir_source_path.clone(),
                backing_directory_subdir: None,
                storage_subdir: None,
            },
            false,
            child_moniker.clone(),
            instance_id.as_ref(),
        )
        .await
        .expect("failed to open isolated storage");

        // ensure the directory is actually open before querying its parent about it.
        dir.describe_deprecated().await.expect("failed to open directory");

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
            false,
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
            false,
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

    #[fuchsia::test]
    fn generate_moniker_based_storage_path_test() {
        for (relative_moniker, expected_output) in vec![
            (vec!["a:1"].try_into().unwrap(), "a:1/data"),
            (vec!["a:1", "b:2"].try_into().unwrap(), "a:1/children/b:2/data"),
            (vec!["a:1", "b:2", "c:3"].try_into().unwrap(), "a:1/children/b:2/children/c:3/data"),
        ] {
            assert_eq!(
                generate_moniker_based_storage_path(&relative_moniker),
                PathBuf::from(expected_output)
            )
        }
    }
}
