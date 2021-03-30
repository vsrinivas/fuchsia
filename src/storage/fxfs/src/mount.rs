use {
    crate::{device::Device, object_store::FxFilesystem},
    anyhow::Error,
    std::sync::Arc,
};

pub async fn mount(device: Arc<dyn Device>) -> Result<Arc<FxFilesystem>, Error> {
    let fs = FxFilesystem::open(device).await?;
    Ok(fs)
}
