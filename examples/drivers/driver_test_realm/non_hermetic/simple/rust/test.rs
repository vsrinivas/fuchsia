use {anyhow::Result, fuchsia_async as fasync};

#[fasync::run_singlethreaded(test)]
async fn test_driver() -> Result<()> {
    let dev = io_util::directory::open_in_namespace(
        "/dev",
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )?;
    device_watcher::recursive_wait_and_open_node(&dev, "sys/test").await?;
    Ok(())
}
