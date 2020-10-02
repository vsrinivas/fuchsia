use {
    fidl_fuchsia_pkg::{
        BlobId, LocalMirrorGetBlobResult, LocalMirrorGetMetadataResult, LocalMirrorMarker,
        LocalMirrorProxy, RepositoryUrl,
    },
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_warn,
    futures::lock::{Mutex, MutexGuard},
    std::sync::Arc,
};

#[derive(Clone, Debug)]
pub struct LocalMirrorWrapper {
    proxy: Arc<Mutex<Option<LocalMirrorProxy>>>,
}

impl LocalMirrorWrapper {
    pub fn new() -> Self {
        Self { proxy: Arc::new(Mutex::new(None)) }
    }

    async fn try_connect(&self) -> Result<MutexGuard<'_, Option<LocalMirrorProxy>>, fidl::Error> {
        {
            let read_lock = self.proxy.lock().await;
            if read_lock.is_some() {
                return Ok(read_lock);
            }
        }
        let conn = connect_to_service::<LocalMirrorMarker>().map_err(|e| {
            fx_log_warn!("Connection failed: {:?}", e);
            fidl::Error::Invalid
        })?;
        {
            let mut write_lock = self.proxy.lock().await;
            if write_lock.is_none() {
                write_lock.replace(conn);
            }
        }

        let read_lock = self.proxy.lock().await;
        return Ok(read_lock);
    }

    pub async fn get_blob(
        &self,
        blob_id: &mut BlobId,
        blob: fidl::endpoints::ServerEnd<fidl_fuchsia_io::FileMarker>,
    ) -> Result<LocalMirrorGetBlobResult, fidl::Error> {
        let proxy_guard = self.try_connect().await?;

        match proxy_guard.as_ref().unwrap().get_blob(blob_id, blob).await {
            Err(e) => {
                std::mem::drop(proxy_guard);
                self.proxy.lock().await.take();
                return Err(e);
            }
            Ok(r) => return Ok(r),
        }
    }

    pub async fn get_metadata(
        &self,
        repo_url: &mut RepositoryUrl,
        path: &str,
        metadata: fidl::endpoints::ServerEnd<fidl_fuchsia_io::FileMarker>,
    ) -> Result<LocalMirrorGetMetadataResult, fidl::Error> {
        let proxy_guard = self.try_connect().await?;
        match proxy_guard.as_ref().unwrap().get_metadata(repo_url, path, metadata).await {
            Err(e) => {
                std::mem::drop(proxy_guard);
                self.proxy.lock().await.take();
                return Err(e);
            }
            Ok(r) => return Ok(r),
        }
    }
}
