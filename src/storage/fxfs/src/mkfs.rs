use {
    crate::{
        device::Device,
        object_store::{filesystem::SyncOptions, FxFilesystem},
        volume::volume_directory,
    },
    anyhow::Error,
    std::sync::Arc,
};

pub async fn mkfs(device: Arc<dyn Device>) -> Result<Arc<FxFilesystem>, Error> {
    let fs = FxFilesystem::new_empty(device).await?;
    let volume_directory = volume_directory(&fs).await?;
    volume_directory.new_volume("default").await?;
    fs.sync(SyncOptions::default()).await?;
    Ok(fs)
}
