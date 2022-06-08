use {anyhow::Result, fuchsia_async as fasync};

// [START example]
#[fasync::run_singlethreaded(test)]
async fn test_driver() -> Result<()> {
    let dev = fuchsia_fs::directory::open_in_namespace(
        "/dev",
        fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
    )?;
    device_watcher::recursive_wait_and_open_node(&dev, "sys/test").await?;
    Ok(())
}
// [END example]
