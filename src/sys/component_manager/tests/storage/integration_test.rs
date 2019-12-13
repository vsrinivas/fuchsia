// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    breakpoint_system_client::*,
    failure::{Error, ResultExt},
    fidl::endpoints::ServerEnd,
    fidl::Channel,
    fidl_fidl_test_components as ftest, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_syslog as syslog, fuchsia_zircon as zx,
    futures::StreamExt,
    io_util::{self, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    lazy_static::lazy_static,
    std::path::PathBuf,
    test_utils::*,
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

#[fasync::run_singlethreaded(test)]
async fn storage() -> Result<(), Error> {
    LOGGER.init();

    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm.cm",
    )
    .await?;

    let breakpoint_system =
        test.connect_to_breakpoint_system().await.expect("breakpoint system is unavailable");
    let receiver = breakpoint_system.set_breakpoints(vec![StartInstance::TYPE]).await?;

    breakpoint_system.start_component_manager().await?;

    // Expect the root component to be bound to
    let invocation = receiver.expect_exact::<StartInstance>("/").await?;
    invocation.resume().await?;

    // Expect the 2 children to be bound to
    let invocation = receiver.expect_type::<StartInstance>().await?;
    invocation.resume().await?;

    let invocation = receiver.expect_type::<StartInstance>().await?;
    invocation.resume().await?;

    let component_manager_path = test.get_component_manager_path();
    let memfs_path =
        component_manager_path.join("out/hub/children/memfs/exec/out/svc/fuchsia.io.Directory");
    let data_path = component_manager_path.join("out/hub/children/storage_user/exec/out/data");

    check_storage(memfs_path, data_path, "storage_user:0").await?;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn storage_from_collection() -> Result<(), Error> {
    LOGGER.init();

    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm_coll.cm",
    )
    .await?;

    let breakpoint_system =
        test.connect_to_breakpoint_system().await.expect("breakpoint system did not connect");
    let receiver = breakpoint_system
        .set_breakpoints(vec![StartInstance::TYPE, PostDestroyInstance::TYPE])
        .await?;
    let bind_receiver = breakpoint_system.set_breakpoints(vec![RouteCapability::TYPE]).await?;

    breakpoint_system.start_component_manager().await?;

    // Expect the root component to be started
    let invocation = receiver.expect_exact::<StartInstance>("/").await?;
    invocation.resume().await?;

    // The root component connects to the Realm service to start the dynamic child
    let invocation = bind_receiver
        .wait_until_framework_capability("/", "/svc/fuchsia.sys2.Realm", Some("/"))
        .await?;
    invocation.resume().await?;

    // Expect 2 children to be started - one static and one dynamic
    // Order is irrelevant
    let invocation = receiver.expect_type::<StartInstance>().await?;
    invocation.resume().await?;

    let invocation = receiver.expect_type::<StartInstance>().await?;
    invocation.resume().await?;

    // With all children started, do the test
    let component_manager_path = test.get_component_manager_path();
    let memfs_path =
        component_manager_path.join("out/hub/children/memfs/exec/out/svc/fuchsia.io.Directory");
    let data_path = component_manager_path.join("out/hub/children/coll:storage_user/exec/out/data");

    check_storage(memfs_path.clone(), data_path, "coll:storage_user:1").await?;

    // The root component connects to the Trigger service to start the dynamic child
    let invocation = bind_receiver
        .wait_until_framework_capability("/", "/svc/fidl.test.components.Trigger", Some("/"))
        .await?;
    invocation.inject(serve_trigger_capability_async()).await?;
    invocation.resume().await?;

    // Expect the dynamic child to be destroyed
    let invocation = receiver.expect_exact::<PostDestroyInstance>("/coll:storage_user:1").await?;

    println!("checking that storage was destroyed");
    let memfs_proxy = io_util::open_directory_in_namespace(
        memfs_path.to_str().unwrap(),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .context("failed to open storage")?;
    assert_eq!(list_directory(&memfs_proxy).await?, Vec::<String>::new());

    invocation.resume().await?;

    Ok(())
}

fn serve_trigger_capability_async() -> Box<dyn Fn(Channel) + Send> {
    Box::new(|channel| {
        let mut stream = ServerEnd::<ftest::TriggerMarker>::new(channel)
            .into_stream()
            .expect("could not convert channel into stream");
        fasync::spawn(async move {
            while let Some(Ok(ftest::TriggerRequest::Run { responder })) = stream.next().await {
                responder.send().unwrap();
            }
        })
    })
}

async fn check_storage(
    memfs_path: PathBuf,
    data_path: PathBuf,
    user_moniker: &str,
) -> Result<(), Error> {
    let memfs_path = memfs_path.to_str().expect("unexpected chars");
    let data_path = data_path.to_str().expect("unexpected chars");

    let child_data_proxy =
        io_util::open_directory_in_namespace(data_path, OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE)
            .context("failed to open storage")?;

    println!("successfully opened \"storage_user\" exposed data directory");

    let file_name = "hippo";
    let file_contents = "hippos_are_neat";

    let file = io_util::open_file(
        &child_data_proxy,
        &PathBuf::from(file_name),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
    )
    .context("failed to open file in storage")?;
    let (s, _) =
        file.write(&mut file_contents.as_bytes().to_vec().into_iter()).await.unwrap_or_else(|_| {
            println!("ERROR! Looping indefinitely");
            loop {}
        });
    assert_eq!(zx::Status::OK, zx::Status::from_raw(s), "writing to the file failed");

    println!("successfully wrote to file \"hippo\" in exposed data directory");

    let memfs_proxy =
        io_util::open_directory_in_namespace(memfs_path, OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE)
            .context("failed to open storage")?;

    println!("successfully opened \"memfs\" exposed directory");

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
    Ok(())
}
