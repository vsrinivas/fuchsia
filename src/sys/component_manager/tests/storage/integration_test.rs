// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{
        model::{
            hooks::*,
            moniker::AbsoluteMoniker,
            testing::{breakpoints::*, test_helpers, test_hook::TestHook},
            ComponentManagerConfig, Model,
        },
        startup,
    },
    failure::{Error, ResultExt},
    fidl::endpoints,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_syslog as syslog, fuchsia_zircon as zx,
    io_util::{self, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    lazy_static::lazy_static,
    std::path::PathBuf,
    std::sync::Arc,
};

lazy_static! {
    static ref LOGGER: Logger = Logger::new();
}

struct Logger {}
impl Logger {
    fn new() -> Self {
        syslog::init_with_tags(&[]).expect("could not initialize logging");
        Self {}
    }

    fn init(&self) {}
}

// TODO: This is a white box test so that we can use hooks. Really this should be a black box test,
// but we need to implement stopping and/or external hooks for that to be possible.

#[fasync::run_singlethreaded(test)]
async fn storage() -> Result<(), Error> {
    LOGGER.init();

    // Set up model and hooks.
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm.cm".to_string();
    let args = startup::Arguments {
        use_builtin_process_launcher: false,
        use_builtin_vmex: false,
        root_component_url,
    };
    let model = startup::model_setup(&args).await?;
    let _builtin_environment =
        startup::builtin_environment_setup(&args, &model, ComponentManagerConfig::default())
            .await?;

    model
        .look_up_and_bind_instance(AbsoluteMoniker::root())
        .await
        .context("could not bind to root realm")?;

    let m = AbsoluteMoniker::from(vec!["storage_user:0"]);
    let storage_user_realm =
        model.look_up_realm(&m).await.context("could not look up storage_user realm")?;
    let (exposed_proxy, server_end) = endpoints::create_proxy::<fio::DirectoryMarker>()?;
    model
        .bind_instance_open_exposed(storage_user_realm, server_end.into_channel())
        .await
        .context("could not open exposed directory of storage user realm")?;

    use_storage(&model, exposed_proxy, "storage_user:0").await?;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn storage_from_collection() -> Result<(), Error> {
    LOGGER.init();

    // Set up model and hooks.
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm.cm".to_string();
    let args = startup::Arguments {
        use_builtin_process_launcher: false,
        use_builtin_vmex: false,
        root_component_url,
    };
    let model = startup::model_setup(&args).await?;
    let builtin_environment =
        startup::builtin_environment_setup(&args, &model, ComponentManagerConfig::default())
            .await?;

    let test_hook = TestHook::new();

    let breakpoint_registry = Arc::new(BreakpointRegistry::new());
    let breakpoint_receiver =
        breakpoint_registry.register(vec![EventType::PostDestroyInstance]).await;
    let breakpoint_hook = BreakpointHook::new(breakpoint_registry.clone());

    model.root_realm.hooks.install(test_hook.hooks()).await;
    model.root_realm.hooks.install(breakpoint_hook.hooks()).await;
    model
        .look_up_and_bind_instance(AbsoluteMoniker::root())
        .await
        .context("could not bind to root realm")?;

    println!("creating and binding to child \"storage_user\"");

    let (realm_proxy, stream) = endpoints::create_proxy_and_stream::<fsys::RealmMarker>().unwrap();
    {
        let realm = model.root_realm.clone();
        fasync::spawn(async move {
            builtin_environment
                .realm_capability_host
                .serve(realm, stream)
                .await
                .expect("failed serving realm service");
        });
    }

    {
        let mut collection_ref = fsys::CollectionRef { name: "coll".to_string() };
        let child_decl = fsys::ChildDecl {
            name: Some("storage_user".to_string()),
            url: Some(
                "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_user.cm"
                    .to_string(),
            ),
            startup: Some(fsys::StartupMode::Lazy),
        };
        let _ = realm_proxy
            .create_child(&mut collection_ref, child_decl)
            .await
            .context("failed to create child")?
            .expect("failed to create child");
    }
    let exposed_proxy = {
        let (exposed_proxy, server_end) =
            endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut child_ref = fsys::ChildRef {
            name: "storage_user".to_string(),
            collection: Some("coll".to_string()),
        };
        realm_proxy
            .bind_child(&mut child_ref, server_end)
            .await
            .context("failed to bind to child")?
            .expect("failed to bind to child");
        exposed_proxy
    };

    let memfs_proxy = use_storage(&model, exposed_proxy, "coll:storage_user:1").await?;

    println!("destroying storage_user");
    let mut child_ref =
        fsys::ChildRef { name: "storage_user".to_string(), collection: Some("coll".to_string()) };
    realm_proxy
        .destroy_child(&mut child_ref)
        .await
        .context("failed to destroy child")?
        .expect("failed to destroy child");

    breakpoint_receiver
        .wait_until(EventType::PostDestroyInstance, vec!["coll:storage_user:1"].into())
        .await;

    println!("checking that storage was destroyed");
    assert_eq!(test_helpers::list_directory(&memfs_proxy).await, Vec::<String>::new());

    Ok(())
}

async fn use_storage(
    model: &Model,
    exposed_proxy: fio::DirectoryProxy,
    user_moniker: &str,
) -> Result<fio::DirectoryProxy, Error> {
    let child_data_proxy = io_util::open_directory(
        &exposed_proxy,
        &PathBuf::from("data"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .context("failed to open storage")?;

    println!("successfully bound to child \"storage_user\"");

    let file_name = "hippo";
    let file_contents = "hippos_are_neat";

    let file = io_util::open_file(
        &child_data_proxy,
        &PathBuf::from(file_name),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
    )
    .context("failed to open file in storage")?;
    let (s, _) = file
        .write(&mut file_contents.as_bytes().to_vec().into_iter())
        .await
        .context("failed to write to file")?;
    assert_eq!(zx::Status::OK, zx::Status::from_raw(s), "writing to the file failed");

    println!("successfully wrote to file \"hippo\" in child's outgoing directory");

    let (memfs_proxy, server_end) = endpoints::create_proxy::<fio::DirectoryMarker>()?;
    let m = AbsoluteMoniker::from(vec!["memfs:0"]);
    let memfs_realm = model.look_up_realm(&m).await.context("could not look up memfs realm")?;
    model
        .bind_instance_open_exposed(memfs_realm, server_end.into_channel())
        .await
        .context("could not open exposed directory of root realm")?;
    let memfs_proxy = io_util::open_directory(
        &memfs_proxy,
        &PathBuf::from("memfs"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )?;

    println!("successfully bound to child \"memfs\"");

    let file_proxy = io_util::open_file(
        &memfs_proxy,
        &PathBuf::from(&format!("{}/data/hippo", user_moniker)),
        OPEN_RIGHT_READABLE,
    )
    .context("failed to open file in memfs")?;
    let read_contents =
        io_util::read_file(&file_proxy).await.context("failed to read file in memfs")?;

    println!("successfully read back contents of file from memfs directly");
    assert_eq!(read_contents, file_contents, "file contents did not match what was written");
    Ok(memfs_proxy)
}
