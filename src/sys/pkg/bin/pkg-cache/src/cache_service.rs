// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::index::{
        fulfill_meta_far_blob, CompleteInstallError, FulfillMetaFarError, PackageIndex,
    },
    anyhow::{anyhow, Error},
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, FileRequest, FileRequestStream},
    fidl_fuchsia_pkg::{
        BlobInfoIteratorRequestStream, NeededBlobsMarker, NeededBlobsRequest,
        NeededBlobsRequestStream, PackageCacheRequest, PackageCacheRequestStream,
        PackageIndexEntry, PackageIndexIteratorRequestStream, PackageUrl,
    },
    fidl_fuchsia_pkg_ext::{serve_fidl_iterator, BlobId, BlobInfo},
    fuchsia_async::Task,
    fuchsia_cobalt::CobaltSender,
    fuchsia_hash::Hash,
    fuchsia_inspect::{self as finspect, NumericProperty, Property, StringProperty},
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_trace as trace, fuchsia_zircon as zx,
    fuchsia_zircon::Status,
    futures::{lock::Mutex, prelude::*, select_biased, stream::FuturesUnordered},
    pkgfs::install::BlobKind,
    std::{
        collections::HashSet,
        sync::{
            atomic::{AtomicU32, Ordering},
            Arc,
        },
    },
    system_image::StaticPackages,
};

pub async fn serve(
    pkgfs_versions: pkgfs::versions::Client,
    pkgfs_ctl: pkgfs::control::Client,
    pkgfs_install: pkgfs::install::Client,
    pkgfs_needs: pkgfs::needs::Client,
    package_index: Arc<Mutex<PackageIndex>>,
    blobfs: blobfs::Client,
    static_packages: Arc<StaticPackages>,
    stream: PackageCacheRequestStream,
    cobalt_sender: CobaltSender,
    serve_id: Arc<AtomicU32>,
    get_node: Arc<finspect::Node>,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| async {
            let cobalt_sender = cobalt_sender.clone();
            match event {
                PackageCacheRequest::Get {
                    meta_far_blob,
                    selectors,
                    needed_blobs,
                    dir,
                    responder,
                } => {
                    let id = serve_id.fetch_add(1, Ordering::SeqCst);
                    let meta_far_blob: BlobInfo = meta_far_blob.into();
                    let node = get_node.create_child(id.to_string());
                    trace::duration_begin!("app", "cache_get",
                        "meta_far_blob_id" => meta_far_blob.blob_id.to_string().as_str()
                    );
                    let response = get(
                        &pkgfs_versions,
                        &pkgfs_install,
                        &pkgfs_needs,
                        &package_index,
                        &blobfs,
                        meta_far_blob,
                        selectors,
                        needed_blobs,
                        dir,
                        cobalt_sender,
                        &node,
                    )
                    .await;
                    trace::duration_end!("app", "cache_get",
                        "status" => Status::from(response).to_string().as_str()
                    );
                    drop(node);
                    responder.send(&mut response.map_err(|status| status.into_raw()))?;
                }
                PackageCacheRequest::Open { meta_far_blob_id, selectors, dir, responder } => {
                    let meta_far_blob_id: BlobId = meta_far_blob_id.into();
                    trace::duration_begin!("app", "cache_open",
                        "meta_far_blob_id" => meta_far_blob_id.to_string().as_str()
                    );
                    let response =
                        open(&pkgfs_versions, meta_far_blob_id, selectors, dir, cobalt_sender)
                            .await;
                    trace::duration_end!("app", "cache_open",
                        "status" => Status::from(response).to_string().as_str()
                    );
                    responder.send(&mut response.map_err(|status| status.into_raw()))?;
                }
                PackageCacheRequest::BasePackageIndex { iterator, control_handle: _ } => {
                    let stream = iterator.into_stream()?;
                    serve_base_package_index(Arc::clone(&static_packages), stream).await;
                }
                PackageCacheRequest::Sync { responder } => {
                    responder.send(&mut pkgfs_ctl.sync().await.map_err(|e| {
                        fx_log_err!("error syncing /pkgfs/ctl: {:#}", anyhow!(e));
                        Status::INTERNAL.into_raw()
                    }))?;
                }
            }

            Ok(())
        })
        .await
}

/// Fetch a package, and optionally open it.
async fn get<'a>(
    pkgfs_versions: &'a pkgfs::versions::Client,
    pkgfs_install: &'a pkgfs::install::Client,
    pkgfs_needs: &'a pkgfs::needs::Client,
    package_index: &Arc<Mutex<PackageIndex>>,
    blobfs: &blobfs::Client,
    meta_far_blob: BlobInfo,
    selectors: Vec<String>,
    needed_blobs: ServerEnd<NeededBlobsMarker>,
    dir_request: Option<ServerEnd<DirectoryMarker>>,
    mut cobalt_sender: CobaltSender,
    node: &finspect::Node,
) -> Result<(), Status> {
    let _time_prop = node.create_int("started-time", zx::Time::get_monotonic().into_nanos());
    let _id_prop = node.create_string("meta-far-id", meta_far_blob.blob_id.to_string());
    let _length_prop = node.create_uint("meta-far-length", meta_far_blob.length);

    if !selectors.is_empty() {
        fx_log_warn!("Get() does not support selectors yet");
    }
    let needed_blobs = needed_blobs.into_stream().map_err(|_| Status::INTERNAL)?;

    let pkg = if let Ok(pkg) = pkgfs_versions.open_package(&meta_far_blob.blob_id.into()).await {
        // If the package can already be opened, it is already cached.
        needed_blobs.control_handle().shutdown_with_epitaph(Status::OK);

        pkg
    } else {
        // Otherwise, go through the process to cache it.
        fx_log_info!("fetching {}", meta_far_blob.blob_id);

        let () = serve_needed_blobs(
            needed_blobs,
            meta_far_blob,
            pkgfs_install,
            pkgfs_needs,
            package_index,
            blobfs,
            node,
        )
        .await
        .map_err(|e| {
            match &e {
                ServeNeededBlobsError::Activate(PokePkgfsError::UnexpectedNeeds(_)) => {
                    cobalt_sender.log_event_count(
                        metrics::PKG_CACHE_UNEXPECTED_PKGFS_NEEDS_METRIC_ID,
                        (),
                        0,
                        1,
                    );
                }
                _ => {}
            }

            fx_log_err!("error while caching package {}: {:#}", meta_far_blob.blob_id, anyhow!(e));

            cobalt_sender.log_event_count(
                metrics::PKG_CACHE_OPEN_METRIC_ID,
                metrics::PkgCacheOpenMetricDimensionResult::Io,
                0,
                1,
            );

            Status::UNAVAILABLE
        })?;

        pkgfs_versions.open_package(&meta_far_blob.blob_id.into()).await.map_err(|err| {
            fx_log_err!(
                "error opening package after fetching it {}: {:#}",
                meta_far_blob.blob_id,
                anyhow!(err)
            );
            cobalt_sender.log_event_count(
                metrics::PKG_CACHE_OPEN_METRIC_ID,
                metrics::PkgCacheOpenMetricDimensionResult::Io,
                0,
                1,
            );
            Status::INTERNAL
        })?
    };

    if let Some(dir_request) = dir_request {
        pkg.reopen(dir_request).map_err(|err| {
            fx_log_err!("error reopening {}: {:#}", meta_far_blob.blob_id, anyhow!(err));
            cobalt_sender.log_event_count(
                metrics::PKG_CACHE_OPEN_METRIC_ID,
                metrics::PkgCacheOpenMetricDimensionResult::Io,
                0,
                1,
            );
            Status::INTERNAL
        })?;
    }

    cobalt_sender.log_event_count(
        metrics::PKG_CACHE_OPEN_METRIC_ID,
        metrics::PkgCacheOpenMetricDimensionResult::Success,
        0,
        1,
    );
    Ok(())
}

/// Open a package directory.
async fn open<'a>(
    pkgfs_versions: &'a pkgfs::versions::Client,
    meta_far_blob_id: BlobId,
    selectors: Vec<String>,
    dir_request: ServerEnd<DirectoryMarker>,
    mut cobalt_sender: CobaltSender,
) -> Result<(), Status> {
    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("Open() does not support selectors yet");
    }

    let pkg =
        pkgfs_versions.open_package(&meta_far_blob_id.into()).await.map_err(|err| match err {
            pkgfs::versions::OpenError::NotFound => {
                cobalt_sender.log_event_count(
                    metrics::PKG_CACHE_OPEN_METRIC_ID,
                    metrics::PkgCacheOpenMetricDimensionResult::NotFound,
                    0,
                    1,
                );
                Status::NOT_FOUND
            }
            err => {
                cobalt_sender.log_event_count(
                    metrics::PKG_CACHE_OPEN_METRIC_ID,
                    metrics::PkgCacheOpenMetricDimensionResult::Io,
                    0,
                    1,
                );
                fx_log_err!("error opening {}: {:?}", meta_far_blob_id, err);
                Status::INTERNAL
            }
        })?;

    pkg.reopen(dir_request).map_err(|err| {
        fx_log_err!("error opening {}: {:#}", meta_far_blob_id, anyhow!(err));
        cobalt_sender.log_event_count(
            metrics::PKG_CACHE_OPEN_METRIC_ID,
            metrics::PkgCacheOpenMetricDimensionResult::Io,
            0,
            1,
        );
        Status::INTERNAL
    })?;

    cobalt_sender.log_event_count(
        metrics::PKG_CACHE_OPEN_METRIC_ID,
        metrics::PkgCacheOpenMetricDimensionResult::Success,
        0,
        1,
    );
    Ok(())
}

#[derive(thiserror::Error, Debug)]
enum ServeNeededBlobsError {
    #[error("protocol violation: request stream terminated unexpectedly in {0}")]
    UnexpectedClose(&'static str),

    #[error("protocol violation: expected {expected} request, got {received}")]
    UnexpectedRequest { received: &'static str, expected: &'static str },

    #[error("protocol violation: while reading next request")]
    ReceiveRequest(#[source] fidl::Error),

    #[error("protocol violation: while responding to last request")]
    SendResponse(#[source] fidl::Error),

    #[error("while opening {context} for write")]
    OpenBlob {
        context: BlobContext,
        #[source]
        source: blobfs::blob::CreateError,
    },

    #[error("while writing {context}")]
    WriteBlob {
        context: BlobContext,
        #[source]
        source: ServeWriteBlobError,
    },

    #[error("the blob {0} is not needed")]
    BlobNotNeeded(Hash),

    #[error("the operation was aborted by the caller")]
    Aborted,

    #[error("while updating package index install state")]
    CompleteInstall(#[from] CompleteInstallError),

    #[error("while updating package index with meta far info")]
    FulfillMetaFar(#[from] FulfillMetaFarError),

    #[error("while activating the package in pkgfs")]
    Activate(#[from] PokePkgfsError),
}

#[derive(Debug)]
struct BlobContext {
    kind: BlobKind,
    hash: Hash,
}

impl BlobContext {
    fn kind_name(&self) -> &'static str {
        match self.kind {
            BlobKind::Package => "metadata",
            BlobKind::Data => "data",
        }
    }
}

impl std::fmt::Display for BlobContext {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} blob ({})", self.kind_name(), self.hash)
    }
}

/// Implements the fuchsia.pkg.NeededBlobs protocol, which represents the transaction for caching a
/// particular package.
///
/// Clients should start by requesting to `OpenMetaBlob()`, and fetch and write the metadata blob
/// if needed. Once written, `GetMissingBlobs()` should be used to determine which content blobs
/// need fetched and written using `OpenBlob()`. Violating the expected protocol state will result
/// in the channel being closed by the package cache with a `ZX_ERR_BAD_STATE` epitaph and aborting
/// the package cache operation.
///
/// Once all needed blobs are written by the client, the package cache will complete the pending
/// [`PackageCache.Get`] request and close this channel with a `ZX_OK` epitaph.
async fn serve_needed_blobs(
    mut stream: NeededBlobsRequestStream,
    meta_far_info: BlobInfo,
    pkgfs_install: &pkgfs::install::Client,
    pkgfs_needs: &pkgfs::needs::Client,
    package_index: &Arc<Mutex<PackageIndex>>,
    blobfs: &blobfs::Client,
    node: &finspect::Node,
) -> Result<(), ServeNeededBlobsError> {
    let state = node.create_string("state", "need-meta-far");
    let res = async {
        // Step 1: Open and write the meta.far, or determine it is not needed.
        let content_blobs =
            handle_open_meta_blob(&mut stream, meta_far_info, blobfs, package_index, &state)
                .await?;

        // Step 2: Determine which data blobs are needed and report them to the client.
        let (serve_iterator, needs) =
            handle_get_missing_blobs(&mut stream, blobfs, &content_blobs).await?;

        state.set("need-content-blobs");

        // Step 3: Open and write all needed data blobs.
        let () = handle_open_blobs(&mut stream, needs, blobfs, &node).await?;

        // Step 4: Start an install for this package through pkgfs, expecting it to discover no
        // work is needed and start serving the package's pkg dir at /pkgfs/versions/<merkle>.
        let () = poke_pkgfs(pkgfs_install, pkgfs_needs, meta_far_info).await?;

        serve_iterator.await;
        Ok(())
    }
    .await;

    if res.is_ok() {
        package_index.lock().await.complete_install(meta_far_info.blob_id.into())?;
    } else {
        package_index.lock().await.cancel_install(&meta_far_info.blob_id.into());
    }

    // TODO in the Err(_) case, a responder was likely dropped, which would have already shutdown
    // the stream without our custom epitaph value.  Need to find a nice way to always shutdown
    // with a custom epitaph without copy/pasting something to every return site.

    let epitaph = match res {
        Ok(_) => Status::OK,
        Err(_) => Status::BAD_STATE,
    };
    stream.control_handle().shutdown_with_epitaph(epitaph);

    res
}

async fn handle_open_meta_blob(
    stream: &mut NeededBlobsRequestStream,
    meta_far_info: BlobInfo,
    blobfs: &blobfs::Client,
    package_index: &Arc<Mutex<PackageIndex>>,
    state: &StringProperty,
) -> Result<HashSet<Hash>, ServeNeededBlobsError> {
    let hash = meta_far_info.blob_id.into();
    package_index.lock().await.start_install(hash);

    loop {
        let (file, responder) =
            match stream.try_next().await.map_err(ServeNeededBlobsError::ReceiveRequest)? {
                Some(NeededBlobsRequest::OpenMetaBlob { file, responder }) => Ok((file, responder)),
                Some(NeededBlobsRequest::Abort { responder: _ }) => {
                    Err(ServeNeededBlobsError::Aborted)
                }
                Some(other) => Err(ServeNeededBlobsError::UnexpectedRequest {
                    received: other.method_name(),
                    expected: "open_meta_blob",
                }),
                None => Err(ServeNeededBlobsError::UnexpectedClose("handle_open_meta_blob")),
            }?;

        let file_stream = file.into_stream().map_err(ServeNeededBlobsError::ReceiveRequest)?;

        match open_write_blob(file_stream, responder, blobfs, hash, BlobKind::Package).await {
            Ok(()) => break,
            Err(OpenWriteBlobError::Serve(e)) => return Err(e),
            Err(OpenWriteBlobError::NonFatalWrite(e)) => {
                fx_log_warn!("Non-fatal error while writing metadata blob: {:#}", anyhow!(e));
                continue;
            }
        }
    }

    state.set("enumerate-missing-blobs");

    let content_blobs = fulfill_meta_far_blob(package_index, blobfs, hash).await?;

    Ok(content_blobs)
}

async fn handle_get_missing_blobs(
    stream: &mut NeededBlobsRequestStream,
    blobfs: &blobfs::Client,
    content_blobs: &HashSet<Hash>,
) -> Result<(Task<()>, HashSet<Hash>), ServeNeededBlobsError> {
    let iterator = match stream.try_next().await.map_err(ServeNeededBlobsError::ReceiveRequest)? {
        Some(NeededBlobsRequest::GetMissingBlobs { iterator, control_handle: _ }) => Ok(iterator),
        Some(NeededBlobsRequest::Abort { responder: _ }) => Err(ServeNeededBlobsError::Aborted),
        Some(other) => Err(ServeNeededBlobsError::UnexpectedRequest {
            received: other.method_name(),
            expected: "get_missing_blobs",
        }),
        None => Err(ServeNeededBlobsError::UnexpectedClose("handle_get_missing_blobs")),
    }?;

    let iter_stream = iterator.into_stream().map_err(ServeNeededBlobsError::ReceiveRequest)?;

    let needs = {
        let mut needs = blobfs
            .filter_to_missing_blobs(&content_blobs)
            .await
            .into_iter()
            .map(|hash| BlobInfo { blob_id: hash.into(), length: 0 })
            .collect::<Vec<_>>();

        // The needs provided by filter_to_missing_blobs are stored in a HashSet, so needs are in
        // an unspecified order here. Provide a deterministic ordering to test/callers by sorting
        // on merkle root.
        needs.sort_unstable();
        needs
    };

    // Start serving the iterator in the background and internally move on to the next state. If
    // this foreground task decides to bail out, this spawned task will be dropped which will abort
    // the iterator serving task.
    let serve_iterator = Task::spawn(serve_blob_info_iterator(
        needs.iter().cloned().map(Into::into).collect::<Vec<fidl_fuchsia_pkg::BlobInfo>>(),
        iter_stream,
    ));
    let needs = needs.into_iter().map(|need| need.blob_id.into()).collect::<HashSet<Hash>>();

    Ok((serve_iterator, needs))
}

async fn handle_open_blobs(
    stream: &mut NeededBlobsRequestStream,
    mut needs: HashSet<Hash>,
    blobfs: &blobfs::Client,
    node: &finspect::Node,
) -> Result<(), ServeNeededBlobsError> {
    let mut writing = FuturesUnordered::new();

    let remaining_counter = node.create_uint("remaining", 0);
    let writing_counter = node.create_uint("writing", 0);
    let written_counter = node.create_uint("written", 0);

    // `needs` represents needed blobs that aren't currently being written
    // `writing` represents needed blobs currently being written
    // A blob write that fails with a retryable error can allow a blob to transition back from
    // `writing` to `needs`.
    // Once both needs and writing are empty, all needed blobs are now present.

    while !(writing.is_empty() && needs.is_empty()) {
        remaining_counter.set(needs.len() as u64);
        writing_counter.set(writing.len() as u64);

        #[derive(Debug)]
        enum Event {
            WriteBlobDone((Hash, Result<(), OpenWriteBlobError>)),
            Request(Option<NeededBlobsRequest>),
        }

        // Wait for the next request/event to happen, giving priority to handling blob write
        // completion events to new incoming requests.
        let event = select_biased! {
            res = writing.select_next_some() => Event::WriteBlobDone(res),
            req = stream.try_next() =>
                Event::Request(req.map_err(ServeNeededBlobsError::ReceiveRequest)?),
        };

        match event {
            Event::Request(Some(NeededBlobsRequest::OpenBlob { blob_id, file, responder })) => {
                let blob_id = Hash::from(BlobId::from(blob_id));

                // Make sure the blob is still needed/isn't being written already.
                if !needs.remove(&blob_id) {
                    return Err(ServeNeededBlobsError::BlobNotNeeded(blob_id));
                }

                let file_stream =
                    file.into_stream().map_err(ServeNeededBlobsError::ReceiveRequest)?;

                // Do the actual async work of opening the blob for write and serving the write
                // calls in a separate Future so this loop can run this Future and handle new
                // requests concurrently.
                let task = open_write_blob(file_stream, responder, blobfs, blob_id, BlobKind::Data);
                writing.push(async move { (blob_id, task.await) });
                continue;
            }

            Event::Request(Some(NeededBlobsRequest::Abort { responder })) => {
                // Finish all currently open blobs before aborting.
                while !writing.is_empty() {
                    writing.next().await;
                }
                drop(responder);
                return Err(ServeNeededBlobsError::Aborted);
            }
            Event::Request(Some(other)) => {
                return Err(ServeNeededBlobsError::UnexpectedRequest {
                    received: other.method_name(),
                    expected: "open_blob",
                })
            }
            Event::Request(None) => {
                return Err(ServeNeededBlobsError::UnexpectedClose("handle_open_blobs"));
            }
            Event::WriteBlobDone((_, Ok(()))) => {
                written_counter.add(1);
                continue;
            }
            Event::WriteBlobDone((_, Err(OpenWriteBlobError::Serve(e)))) => {
                return Err(e);
            }
            Event::WriteBlobDone((hash, Err(OpenWriteBlobError::NonFatalWrite(e)))) => {
                // TODO serve_write_blob will notify the client of the error before this task
                // finishes, so it is possible for a client to retry a blob fetch before this task
                // re-registers the blob as needed, which would be a race condition if the
                // pkg-resolver didn't just abort package fetches on any error.
                fx_log_warn!("Non-fatal error while writing content blob: {:#}", anyhow!(e));
                needs.insert(hash);
                continue;
            }
        }
    }

    Ok(())
}

async fn poke_pkgfs(
    pkgfs_install: &pkgfs::install::Client,
    pkgfs_needs: &pkgfs::needs::Client,
    meta_far_info: BlobInfo,
) -> Result<(), PokePkgfsError> {
    // Try to create the meta far blob, expecting it to fail with already_exists, indicating the
    // meta far blob is readable in blobfs. Pkgfs will then import the package, populating
    // /pkgfs/needs/<merkle> with any missing content blobs, which we expect to be empty.
    let () = match pkgfs_install.create_blob(meta_far_info.blob_id.into(), BlobKind::Package).await
    {
        Err(pkgfs::install::BlobCreateError::AlreadyExists) => Ok(()),
        Ok((_blob, closer)) => {
            let () = closer.close().await;
            Err(PokePkgfsError::MetaFarWritable)
        }
        Err(e) => Err(PokePkgfsError::UnexpectedMetaFarCreateError(e)),
    }?;

    // Verify that /pkgfs/needs/<merkle> is empty or missing, failing with its contents if it is
    // not.
    let needs = {
        let needs = pkgfs_needs.list_needs(meta_far_info.blob_id.into());
        futures::pin_mut!(needs);
        match needs.try_next().await.map_err(PokePkgfsError::ListNeeds)? {
            Some(needs) => {
                let mut needs = needs.into_iter().collect::<Vec<_>>();
                needs.sort_unstable();
                needs
            }
            None => vec![],
        }
    };
    if !needs.is_empty() {
        return Err(PokePkgfsError::UnexpectedNeeds(needs));
    }

    Ok(())
}

#[derive(thiserror::Error, Debug)]
enum PokePkgfsError {
    #[error("the meta far should be read-only, but it is writable")]
    MetaFarWritable,

    #[error("the meta far failed to open with an unexpected error")]
    UnexpectedMetaFarCreateError(#[source] pkgfs::install::BlobCreateError),

    #[error("while listing needs")]
    ListNeeds(#[source] pkgfs::needs::ListNeedsError),

    #[error("the package should have all blobs present on disk, but some were not ({0:?})")]
    UnexpectedNeeds(Vec<Hash>),
}

#[derive(Debug)]
enum OpenWriteBlobError {
    NonFatalWrite(ServeWriteBlobError),
    Serve(ServeNeededBlobsError),
}
impl From<ServeNeededBlobsError> for OpenWriteBlobError {
    fn from(e: ServeNeededBlobsError) -> Self {
        OpenWriteBlobError::Serve(e)
    }
}

// Allow a function to generically respond to either an OpenMetaBlob or OpenBlob request.
type OpenBlobResponse = Result<bool, fidl_fuchsia_pkg::OpenBlobError>;
trait OpenBlobResponder {
    fn send(self, res: OpenBlobResponse) -> Result<(), fidl::Error>;
}
impl OpenBlobResponder for fidl_fuchsia_pkg::NeededBlobsOpenBlobResponder {
    fn send(self, mut res: OpenBlobResponse) -> Result<(), fidl::Error> {
        self.send(&mut res)
    }
}
impl OpenBlobResponder for fidl_fuchsia_pkg::NeededBlobsOpenMetaBlobResponder {
    fn send(self, mut res: OpenBlobResponse) -> Result<(), fidl::Error> {
        self.send(&mut res)
    }
}

async fn open_write_blob(
    file_stream: FileRequestStream,
    responder: impl OpenBlobResponder,
    blobfs: &blobfs::Client,
    blob_id: Hash,
    kind: BlobKind,
) -> Result<(), OpenWriteBlobError> {
    let create_res = blobfs.open_blob_for_write(&blob_id).await;

    let is_readable = match &create_res {
        Err(blobfs::blob::CreateError::AlreadyExists) => {
            // The blob may exist and be readable, or it may be in the process of being written.
            // Ensure we only indicate the blob is already present if we can actually open it for
            // read.
            blobfs.has_blob(&blob_id).await
        }
        _ => false,
    };

    // Always respond to the Open[Meta]Blob request, then worry about actually handling the result.
    responder
        .send(match &create_res {
            Ok(_) => Ok(true),
            Err(blobfs::blob::CreateError::AlreadyExists) if is_readable => Ok(false),
            Err(blobfs::blob::CreateError::AlreadyExists) => {
                Err(fidl_fuchsia_pkg::OpenBlobError::ConcurrentWrite)
            }
            Err(blobfs::blob::CreateError::Io(_)) => {
                Err(fidl_fuchsia_pkg::OpenBlobError::UnspecifiedIo)
            }
        })
        .map_err(ServeNeededBlobsError::SendResponse)?;

    match create_res {
        Ok(blob) => serve_write_blob(file_stream, blob).await.map_err(|e| {
            if e.is_fatal() {
                OpenWriteBlobError::Serve(ServeNeededBlobsError::WriteBlob {
                    context: BlobContext { kind, hash: blob_id },
                    source: e,
                })
            } else {
                OpenWriteBlobError::NonFatalWrite(e)
            }
        }),
        Err(blobfs::blob::CreateError::AlreadyExists) if is_readable => Ok(()),
        Err(blobfs::blob::CreateError::AlreadyExists) => {
            Err(OpenWriteBlobError::NonFatalWrite(ServeWriteBlobError::ConcurrentWrite))
        }
        Err(e @ blobfs::blob::CreateError::Io(_)) => Err(ServeNeededBlobsError::OpenBlob {
            context: BlobContext { kind, hash: blob_id },
            source: e,
        }
        .into()),
    }
}

#[derive(thiserror::Error, Debug)]
enum ServeWriteBlobError {
    #[error("protocol violation: file request stream terminated unexpectedly")]
    UnexpectedClose,

    #[error("protocol violation: file request stream fidl error")]
    Fidl(#[source] fidl::Error),

    #[error("protocol violation: expected {expected} request, got {received}")]
    UnexpectedRequest { received: &'static str, expected: &'static str },

    #[error("insufficient storage space is available")]
    NoSpace,

    #[error("the provided blob data is corrupt")]
    Corrupt,

    #[error("the blob is in the process of being written")]
    ConcurrentWrite,

    #[error("while truncating the blob")]
    Truncate(#[source] blobfs::blob::TruncateError),

    #[error("while writing to the blob")]
    Write(#[source] blobfs::blob::WriteError),
}

impl From<blobfs::blob::TruncateError> for ServeWriteBlobError {
    fn from(e: blobfs::blob::TruncateError) -> Self {
        match e {
            blobfs::blob::TruncateError::NoSpace => ServeWriteBlobError::NoSpace,
            blobfs::blob::TruncateError::Corrupt => ServeWriteBlobError::Corrupt,
            blobfs::blob::TruncateError::ConcurrentWrite => ServeWriteBlobError::ConcurrentWrite,
            e => ServeWriteBlobError::Truncate(e),
        }
    }
}

impl From<blobfs::blob::WriteError> for ServeWriteBlobError {
    fn from(e: blobfs::blob::WriteError) -> Self {
        match e {
            blobfs::blob::WriteError::NoSpace => ServeWriteBlobError::NoSpace,
            blobfs::blob::WriteError::Corrupt => ServeWriteBlobError::Corrupt,
            e => ServeWriteBlobError::Write(e),
        }
    }
}

impl ServeWriteBlobError {
    /// Determines if this error should cancel the associated Get() operation (true) or should
    /// allow the NeededBlobs client retry the operation later (false).
    fn is_fatal(&self) -> bool {
        match self {
            ServeWriteBlobError::UnexpectedClose => false,
            ServeWriteBlobError::Fidl(_) => true,
            ServeWriteBlobError::UnexpectedRequest { .. } => true,
            ServeWriteBlobError::NoSpace => false,
            ServeWriteBlobError::Corrupt => false,
            ServeWriteBlobError::ConcurrentWrite => false,
            ServeWriteBlobError::Truncate(_) => true,
            ServeWriteBlobError::Write(_) => true,
        }
    }
}

async fn serve_write_blob(
    mut stream: FileRequestStream,
    blob: blobfs::blob::Blob<blobfs::blob::NeedsTruncate>,
) -> Result<(), ServeWriteBlobError> {
    use blobfs::blob::{
        Blob, NeedsData, NeedsTruncate, TruncateError, TruncateSuccess, WriteError, WriteSuccess,
    };

    let closer = blob.closer();

    enum State {
        ExpectTruncate(Blob<NeedsTruncate>),
        ExpectData(Blob<NeedsData>),
        ExpectClose,
    }

    impl State {
        fn expectation(&self) -> &'static str {
            match self {
                State::ExpectTruncate(_) => "truncate",
                State::ExpectData(_) => "write",
                State::ExpectClose => "close",
            }
        }
    }

    // Allow the inner task to sometimes close the underlying blob early while also unconditionally
    // calling close after the inner task completes.  Close closes the underlying blob the first
    // time it is called and becomes a no-op on later calls.
    let mut closer = Some(closer);
    let mut close = || {
        let closer = closer.take().map(|closer| closer.close());
        async move {
            match closer {
                Some(closer) => closer.await,
                None => {}
            }
        }
    };

    let mut state = State::ExpectTruncate(blob);

    let task = async {
        while let Some(request) = stream.try_next().await.map_err(ServeWriteBlobError::Fidl)? {
            state = match (request, state) {
                (FileRequest::Truncate { length, responder }, State::ExpectTruncate(blob)) => {
                    let res = blob.truncate(length).await;

                    // Interpret responding errors as the stream closing unexpectedly.
                    let _ = responder.send(
                        match &res {
                            Ok(_) => Status::OK,
                            Err(TruncateError::NoSpace) => Status::NO_SPACE,
                            Err(TruncateError::Corrupt) => Status::IO_DATA_INTEGRITY,
                            Err(TruncateError::ConcurrentWrite)
                            | Err(TruncateError::Fidl(_))
                            | Err(TruncateError::UnexpectedResponse(_)) => Status::INTERNAL,
                        }
                        .into_raw(),
                    );

                    // The empty blob needs no data and is complete after it is truncated.
                    match res? {
                        TruncateSuccess::Done(_) => State::ExpectClose,
                        TruncateSuccess::NeedsData(blob) => State::ExpectData(blob),
                    }
                }

                (FileRequest::Write { data, responder }, State::ExpectData(blob)) => {
                    let res = blob.write(&data).await;

                    let _ = responder.send(
                        match &res {
                            Ok(_) => Status::OK,
                            Err(WriteError::NoSpace) => Status::NO_SPACE,
                            Err(WriteError::Corrupt) => Status::IO_DATA_INTEGRITY,
                            Err(WriteError::Overwrite) => Status::IO,
                            Err(WriteError::Fidl(_)) | Err(WriteError::UnexpectedResponse(_)) => {
                                Status::INTERNAL
                            }
                        }
                        .into_raw(),
                        data.len() as u64,
                    );

                    match res? {
                        WriteSuccess::MoreToWrite(blob) => State::ExpectData(blob),
                        WriteSuccess::Done(_blob) => State::ExpectClose,
                    }
                }

                // Close is allowed in any state, but the blob is only written if we were expecting
                // a close.
                (FileRequest::Close { responder }, State::ExpectClose) => {
                    close().await;
                    let _ = responder.send(Status::OK.into_raw());
                    return Ok(());
                }
                (FileRequest::Close2 { responder }, State::ExpectClose) => {
                    close().await;
                    let _ = responder.send(&mut Ok(()));
                    return Ok(());
                }
                (FileRequest::Close { responder }, _) => {
                    close().await;
                    let _ = responder.send(Status::OK.into_raw());
                    return Err(ServeWriteBlobError::UnexpectedClose);
                }
                (FileRequest::Close2 { responder }, _) => {
                    close().await;
                    let _ = responder.send(&mut Ok(()));
                    return Err(ServeWriteBlobError::UnexpectedClose);
                }

                (request, state) => {
                    return Err(ServeWriteBlobError::UnexpectedRequest {
                        received: request.method_name(),
                        expected: state.expectation(),
                    });
                }
            };
        }

        match state {
            State::ExpectClose => Ok(()),
            _ => Err(ServeWriteBlobError::UnexpectedClose),
        }
    };

    // Handle the request stream, then close the blob, then close the stream to avoid retry races
    // creating a blob that is still open.
    let res = task.await;
    close().await;
    drop(stream);

    res
}

/// Serves the `PackageIndexIteratorRequestStream` with as many entries per request as will fit in
/// a fidl message.
async fn serve_base_package_index(
    static_packages: Arc<StaticPackages>,
    stream: PackageIndexIteratorRequestStream,
) {
    let package_entries = static_packages
        .contents()
        .map(|(path, hash)| PackageIndexEntry {
            package_url: PackageUrl { url: format!("fuchsia-pkg://fuchsia.com/{}", path.name()) },
            meta_far_blob_id: BlobId::from(hash.clone()).into(),
        })
        .collect::<Vec<PackageIndexEntry>>();
    serve_fidl_iterator(package_entries, stream).await.unwrap_or_else(|e| {
        fx_log_err!("error serving PackageIndexIteratorRequestStream protocol: {:#}", anyhow!(e))
    })
}

/// Serves the `BlobInfoIteratorRequestStream` with as many entries per request as will fit in a
/// fidl message.
async fn serve_blob_info_iterator(
    items: impl AsMut<[fidl_fuchsia_pkg::BlobInfo]>,
    stream: BlobInfoIteratorRequestStream,
) {
    serve_fidl_iterator(items, stream).await.unwrap_or_else(|e| {
        fx_log_err!("error serving BlobInfoIteratorRequestStream protocol: {:#}", anyhow!(e))
    })
}

#[cfg(test)]
mod serve_needed_blobs_tests {
    use {
        super::*,
        fidl_fuchsia_io::FileMarker,
        fidl_fuchsia_pkg::{BlobInfoIteratorMarker, BlobInfoIteratorProxy, NeededBlobsProxy},
        fuchsia_async::Timer,
        fuchsia_hash::HashRangeFull,
        fuchsia_inspect as finspect,
        futures::future::Either,
        matches::assert_matches,
        std::time::Duration,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_stop() {
        let (_, stream) = fidl::endpoints::create_proxy_and_stream::<NeededBlobsMarker>().unwrap();

        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };

        let (pkgfs_install, _) = pkgfs::install::Client::new_test();
        let (pkgfs_needs, _) = pkgfs::needs::Client::new_test();
        let (blobfs, _) = blobfs::Client::new_test();
        let inspector = finspect::Inspector::new();
        let package_index = Arc::new(Mutex::new(PackageIndex::new(
            inspector.root().create_child("test_does_not_use_inspect "),
        )));

        assert_matches!(
            serve_needed_blobs(
                stream,
                meta_blob_info,
                &pkgfs_install,
                &pkgfs_needs,
                &package_index,
                &blobfs,
                &inspector.root().create_child("test-node-name")
            )
            .await,
            Err(ServeNeededBlobsError::UnexpectedClose("handle_open_meta_blob"))
        );
    }

    fn spawn_serve_needed_blobs_with_mocks(
        meta_blob_info: BlobInfo,
    ) -> (
        Task<Result<(), ServeNeededBlobsError>>,
        NeededBlobsProxy,
        pkgfs::install::Mock,
        pkgfs::needs::Mock,
        blobfs::Mock,
    ) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<NeededBlobsMarker>().unwrap();

        let (pkgfs_install, pkgfs_install_mock) = pkgfs::install::Client::new_mock();
        let (pkgfs_needs, pkgfs_needs_mock) = pkgfs::needs::Client::new_mock();
        let (blobfs, blobfs_mock) = blobfs::Client::new_mock();
        let inspector = finspect::Inspector::new();
        let package_index = Arc::new(Mutex::new(PackageIndex::new(
            inspector.root().create_child("test_does_not_use_inspect "),
        )));

        (
            Task::spawn(async move {
                serve_needed_blobs(
                    stream,
                    meta_blob_info,
                    &pkgfs_install,
                    &pkgfs_needs,
                    &package_index,
                    &blobfs,
                    &inspector.root().create_child("test-node-name"),
                )
                .await
            }),
            proxy,
            pkgfs_install_mock,
            pkgfs_needs_mock,
            blobfs_mock,
        )
    }

    struct FakeOpenBlobResponse(Option<OpenBlobResponse>);

    struct FakeOpenBlobResponder<'a> {
        // Response is written to through send(). It is never intended to read.
        #[allow(dead_code)]
        response: &'a mut FakeOpenBlobResponse,
    }

    impl FakeOpenBlobResponse {
        fn new() -> Self {
            Self(None)
        }
        fn responder(&mut self) -> FakeOpenBlobResponder<'_> {
            FakeOpenBlobResponder { response: self }
        }
        fn take(self) -> OpenBlobResponse {
            self.0.unwrap()
        }
    }

    impl OpenBlobResponder for FakeOpenBlobResponder<'_> {
        fn send(self, res: OpenBlobResponse) -> Result<(), fidl::Error> {
            self.response.0 = Some(res);
            Ok(())
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_write_blob_handles_io_open_error() {
        // Provide open_write_blob a closed blobfs and file stream to trigger a PEER_CLOSED IO
        // error.
        let (_, file_stream) = fidl::endpoints::create_request_stream::<FileMarker>().unwrap();
        let (blobfs, _) = blobfs::Client::new_test();

        let mut response = FakeOpenBlobResponse::new();
        let res = open_write_blob(
            file_stream,
            response.responder(),
            &blobfs,
            [0; 32].into(),
            BlobKind::Package,
        )
        .await;

        // The operation should fail, and it should report the failure to the fidl responder.
        assert_matches!(
            res,
            Err(OpenWriteBlobError::Serve(ServeNeededBlobsError::OpenBlob {
                context: BlobContext { kind: BlobKind::Package, .. },
                source: blobfs::blob::CreateError::Io(_),
            }))
        );
        assert_eq!(response.take(), Err(fidl_fuchsia_pkg::OpenBlobError::UnspecifiedIo));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn expects_open_meta_blob() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };

        let (task, proxy, pkgfs_install, pkgfs_needs, blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let (iter, iter_server_end) =
            fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
        proxy.get_missing_blobs(iter_server_end).unwrap();
        assert_matches!(iter.next().await, Err(_));

        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "get_missing_blobs",
                expected: "open_meta_blob"
            })
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn expects_open_meta_blob_once() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 4 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        // Open a needed meta FAR blob and write it.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([0; 32].into()).await.expect_payload(b"test").await;

                // serve_needed_blobs parses the meta far after it is written.  Feed that logic a
                // valid, minimal far that doesn't actually correlate to what we just wrote.
                serve_minimal_far(&mut blobfs, [0; 32].into()).await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(true)));

                let _ = blob.truncate(4).await;
                let _ = blob.write(b"test").await;
                let _ = blob.close().await;
            },
        )
        .await;

        // Trying to open the meta FAR blob again after writing it successfully is a protocol violation.
        let (_blob, blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        assert_matches!(proxy.open_meta_blob(blob_server_end).await, Err(_));

        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "open_meta_blob",
                expected: "get_missing_blobs"
            })
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_present_meta_blob() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        // Try to open the meta FAR blob, but report it is no longer needed.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([0; 32].into()).await.fail_open_with_already_exists();
                blobfs
                    .expect_open_blob([0; 32].into())
                    .await
                    .succeed_open_with_blob_readable()
                    .await;

                // serve_needed_blobs parses the meta far after it is written.  Feed that logic a
                // valid, minimal far that doesn't actually correlate to what we just wrote.
                serve_minimal_far(&mut blobfs, [0; 32].into()).await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(false)));
                assert_matches!(blob.truncate(0).await, Err(_));
            },
        )
        .await;

        // Trying to open the meta FAR blob again after being told it is not needed is a protocol
        // violation.
        let (_blob, blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        assert_matches!(proxy.open_meta_blob(blob_server_end).await, Err(_));

        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "open_meta_blob",
                expected: "get_missing_blobs"
            })
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn allows_retrying_nonfatal_open_meta_blob_errors() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 1 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        // Try to open the meta FAR blob, but report it is already being written concurrently.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([0; 32].into()).await.fail_open_with_already_exists();
                blobfs.expect_open_blob([0; 32].into()).await.fail_open_with_not_readable().await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_meta_blob(blob_server_end).await,
                    Ok(Err(fidl_fuchsia_pkg::OpenBlobError::ConcurrentWrite))
                );
                assert_matches!(blob.truncate(1).await, Err(_));
            },
        )
        .await;

        // Try to write the meta FAR blob, but report the written contents are corrupt.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([0; 32].into()).await.fail_write_with_corrupt().await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(true)));

                let _ = blob.truncate(1).await;
                let _ = blob.write(&mut [0]).await;
                let _ = blob.close().await;
            },
        )
        .await;

        // Open the meta FAR blob for write, but then close it (a non-fatal error)
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([0; 32].into()).await.expect_close().await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(true)));

                let _ = blob.close().await;
            },
        )
        .await;

        // Operation succeeds after blobfs cooperates.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([0; 32].into()).await.expect_payload(&[0]).await;

                // serve_needed_blobs parses the meta far after it is written.  Feed that logic a
                // valid, minimal far that doesn't actually correlate to what we just wrote.
                serve_minimal_far(&mut blobfs, [0; 32].into()).await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(true)));

                let _ = blob.truncate(1).await;
                let _ = blob.write(&mut [0]).await;
                let _ = blob.close().await;
            },
        )
        .await;

        // Task moves to next state after retried write operation succeeds.
        let (_blob, blob_server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        assert_matches!(proxy.open_meta_blob(blob_server_end).await, Err(_));
        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "open_meta_blob",
                expected: "get_missing_blobs"
            })
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    pub(super) async fn serve_minimal_far(blobfs: &mut blobfs::Mock, meta_hash: Hash) {
        let far_data = crate::test_utils::get_meta_far("fake-package", vec![]);

        blobfs.expect_open_blob(meta_hash.into()).await.serve_contents(&far_data[..]).await;
    }

    pub(super) async fn write_meta_blob(
        proxy: &NeededBlobsProxy,
        blobfs: &mut blobfs::Mock,
        meta_blob_info: BlobInfo,
        needed_blobs: impl IntoIterator<Item = Hash>,
    ) {
        let far_data = crate::test_utils::get_meta_far("fake-package", needed_blobs);

        let ((), ()) = future::join(
            async {
                // Fail the create request, then succeed an open request that checks if the blob is
                // readable. The already_exists error could indicate that the blob is being
                // written, so pkg-cache need to disambiguate the 2 cases.
                blobfs
                    .expect_create_blob(meta_blob_info.blob_id.into())
                    .await
                    .fail_open_with_already_exists();
                blobfs
                    .expect_open_blob(meta_blob_info.blob_id.into())
                    .await
                    .succeed_open_with_blob_readable()
                    .await;

                blobfs
                    .expect_open_blob(meta_blob_info.blob_id.into())
                    .await
                    .serve_contents(&far_data[..])
                    .await;
            },
            async {
                let (_blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(false)));
            },
        )
        .await;
    }

    pub(super) async fn succeed_poke_pkgfs(
        pkgfs_install: &mut pkgfs::install::Mock,
        pkgfs_needs: &mut pkgfs::needs::Mock,
        meta_blob_info: BlobInfo,
    ) {
        let meta_hash = meta_blob_info.blob_id.into();

        pkgfs_install
            .expect_create_blob(meta_hash, BlobKind::Package.into())
            .await
            .fail_open_with_already_exists();

        pkgfs_needs.expect_enumerate_needs(meta_hash).await.fail_open_with_not_found().await;
    }

    async fn collect_blob_info_iterator(proxy: BlobInfoIteratorProxy) -> Vec<BlobInfo> {
        let mut res = vec![];

        loop {
            let chunk = proxy.next().await.unwrap();

            if chunk.is_empty() {
                break;
            }

            res.extend(chunk.into_iter().map(BlobInfo::from));
        }

        res
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn discovers_and_reports_missing_blobs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let expected = HashRangeFull::default().skip(1).take(2000).collect::<Vec<_>>();

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, expected.iter().copied()).await;

        let ((), ()) = future::join(
            async {
                blobfs
                    .expect_filter_to_missing_blobs_with_readable_missing_ids(&[], &expected[..])
                    .await;
            },
            async {
                let (missing_blobs_iter, missing_blobs_iter_server_end) =
                    fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();

                assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));

                let missing_blobs = collect_blob_info_iterator(missing_blobs_iter).await;

                let expected = expected
                    .iter()
                    .cloned()
                    .map(|hash| BlobInfo { blob_id: hash.into(), length: 0 })
                    .collect::<Vec<_>>();
                assert_eq!(missing_blobs, expected);
            },
        )
        .await;

        drop(proxy);
        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::UnexpectedClose("handle_open_blobs"))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_no_missing_blobs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, mut pkgfs_install, mut pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![]).await;

        let (missing_blobs_iter, missing_blobs_iter_server_end) =
            fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
        assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));
        let missing_blobs = collect_blob_info_iterator(missing_blobs_iter).await;
        assert_eq!(missing_blobs, vec![]);

        succeed_poke_pkgfs(&mut pkgfs_install, &mut pkgfs_needs, meta_blob_info).await;

        assert_matches!(task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fails_on_invalid_meta_far() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let bogus_far_data = b"this is not a far file";

        let ((), ()) = future::join(
            async {
                // Fail the create request, then succeed an open request that checks if the blob is
                // readable. The already_exists error could indicate that the blob is being
                // written, so pkg-cache need to disambiguate the 2 cases.
                blobfs
                    .expect_create_blob(meta_blob_info.blob_id.into())
                    .await
                    .fail_open_with_already_exists();
                blobfs
                    .expect_open_blob(meta_blob_info.blob_id.into())
                    .await
                    .succeed_open_with_blob_readable()
                    .await;

                blobfs
                    .expect_open_blob(meta_blob_info.blob_id.into())
                    .await
                    .serve_contents(&bogus_far_data[..])
                    .await;
            },
            async {
                let (_blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(false)));
            },
        )
        .await;

        drop(proxy);
        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::FulfillMetaFar(FulfillMetaFarError::QueryPackageMetadata(
                _
            )))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fails_on_pkgfs_install_unexpected_create_error() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, mut pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![]).await;

        let (missing_blobs_iter, missing_blobs_iter_server_end) =
            fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
        assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));
        let missing_blobs = collect_blob_info_iterator(missing_blobs_iter).await;
        assert_eq!(missing_blobs, vec![]);

        // Indicate the meta far is not present, even though we just wrote it.
        pkgfs_install
            .expect_create_blob(meta_blob_info.blob_id.into(), BlobKind::Package.into())
            .await
            .fail_open_with_concurrent_write();

        drop(proxy);
        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::Activate(PokePkgfsError::UnexpectedMetaFarCreateError(_)))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fails_on_needs_enumeration_error() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, mut pkgfs_install, mut pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![]).await;

        let (missing_blobs_iter, missing_blobs_iter_server_end) =
            fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
        assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));
        let missing_blobs = collect_blob_info_iterator(missing_blobs_iter).await;
        assert_eq!(missing_blobs, vec![]);

        // Indicate the meta far is present, but then fail to enumerate needs in an unexpected way.
        pkgfs_install
            .expect_create_blob(meta_blob_info.blob_id.into(), BlobKind::Package.into())
            .await
            .fail_open_with_already_exists();
        pkgfs_needs
            .expect_enumerate_needs(meta_blob_info.blob_id.into())
            .await
            .fail_open_with_unexpected_error()
            .await;

        drop(proxy);
        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::Activate(PokePkgfsError::ListNeeds(_)))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fails_on_any_pkgfs_needs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, mut pkgfs_install, mut pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![]).await;

        let (missing_blobs_iter, missing_blobs_iter_server_end) =
            fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
        assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));
        let missing_blobs = collect_blob_info_iterator(missing_blobs_iter).await;
        assert_eq!(missing_blobs, vec![]);

        // Indicate the meta far is present, but then indicate that some blobs are needed, which
        // shouldn't be possible.
        let unexpected_needs = HashRangeFull::default().take(10).collect::<Vec<_>>();
        pkgfs_install
            .expect_create_blob(meta_blob_info.blob_id.into(), BlobKind::Package.into())
            .await
            .fail_open_with_already_exists();
        pkgfs_needs
            .expect_enumerate_needs(meta_blob_info.blob_id.into())
            .await
            .enumerate_needs(unexpected_needs.iter().copied().collect())
            .await;

        drop(proxy);
        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::Activate(PokePkgfsError::UnexpectedNeeds(needs))) if needs == unexpected_needs
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn dropping_needed_blobs_stops_missing_blob_iterator() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let missing = HashRangeFull::default().take(10).collect::<Vec<_>>();
        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, missing.iter().copied()).await;

        let ((), ()) = future::join(
            async {
                blobfs
                    .expect_filter_to_missing_blobs_with_readable_missing_ids(&[], &missing[..])
                    .await;
            },
            async {
                let (missing_blobs_iter, missing_blobs_iter_server_end) =
                    fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();

                assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));

                // Closing the needed blobs request stream terminates any spawned tasks.
                drop(proxy);
                assert_matches!(
                    missing_blobs_iter.next().await,
                    Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
                );
            },
        )
        .await;

        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::UnexpectedClose("handle_open_blobs"))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn expects_get_missing_blobs_once() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let missing = HashRangeFull::default().take(10).collect::<Vec<_>>();
        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, missing.iter().copied()).await;

        // Enumerate the needs successfully once.
        let ((), ()) = future::join(
            async {
                blobfs
                    .expect_filter_to_missing_blobs_with_readable_missing_ids(&[], &missing[..])
                    .await;
            },
            async {
                let (missing_blobs_iter, missing_blobs_iter_server_end) =
                    fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();

                assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));

                collect_blob_info_iterator(missing_blobs_iter).await;
            },
        )
        .await;

        // Trying to enumerate the missing blobs again is a protocol violation.
        let (_missing_blobs_iter, missing_blobs_iter_server_end) =
            fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
        assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));

        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "get_missing_blobs",
                expected: "open_blob"
            })
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    pub(super) async fn enumerate_readable_missing_blobs(
        proxy: &NeededBlobsProxy,
        blobfs: &mut blobfs::Mock,
        readable: impl Iterator<Item = Hash>,
        missing: impl Iterator<Item = Hash>,
    ) {
        let readable = readable.collect::<Vec<_>>();
        let missing = missing.collect::<Vec<_>>();

        let ((), ()) = future::join(
            async {
                blobfs
                    .expect_filter_to_missing_blobs_with_readable_missing_ids(
                        &readable[..],
                        &missing[..],
                    )
                    .await;
            },
            async {
                let (missing_blobs_iter, missing_blobs_iter_server_end) =
                    fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();

                assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));

                let infos = collect_blob_info_iterator(missing_blobs_iter).await;
                let mut actual =
                    infos.into_iter().map(|info| info.blob_id.into()).collect::<Vec<Hash>>();
                actual.sort_unstable();
                assert_eq!(missing, actual);
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn single_need() {
        let meta_blob_info = BlobInfo { blob_id: [1; 32].into(), length: 0 };
        let (task, proxy, mut pkgfs_install, mut pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![[2; 32].into()]).await;
        enumerate_readable_missing_blobs(
            &proxy,
            &mut blobfs,
            std::iter::empty(),
            vec![[2; 32].into()].into_iter(),
        )
        .await;

        let payload = b"single blob";

        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([2; 32].into()).await.expect_payload(payload).await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from([2; 32]).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let _ = blob.truncate(payload.len() as u64).await;
                let _ = blob.write(payload).await;
                let _ = blob.close().await;
            },
        )
        .await;

        succeed_poke_pkgfs(&mut pkgfs_install, &mut pkgfs_needs, meta_blob_info).await;

        assert_matches!(task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn expects_open_blob_per_blob_once() {
        let meta_blob_info = BlobInfo { blob_id: [1; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![[2; 32].into()]).await;
        enumerate_readable_missing_blobs(
            &proxy,
            &mut blobfs,
            std::iter::empty(),
            vec![[2; 32].into()].into_iter(),
        )
        .await;

        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([2; 32].into()).await.expect_close().await;
            },
            async {
                let (_blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from([2; 32]).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let (_blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();
                assert_matches!(
                    proxy.open_blob(&mut BlobId::from([2; 32]).into(), blob_server_end).await,
                    Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
                );
            },
        )
        .await;

        assert_matches!(task.await, Err(ServeNeededBlobsError::BlobNotNeeded(hash)) if hash == [2; 32].into());
        assert_matches!(proxy.take_event_stream().next().await, None);
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_many_content_blobs_that_need_written() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, mut pkgfs_install, mut pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let content_blobs = || HashRangeFull::default().skip(1).take(100);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, content_blobs()).await;
        enumerate_readable_missing_blobs(&proxy, &mut blobfs, std::iter::empty(), content_blobs())
            .await;

        fn payload(hash: Hash) -> Vec<u8> {
            let hash_bytes = || hash.as_bytes().iter().copied();
            let len = hash_bytes().map(|n| n as usize).sum();
            assert!(len <= fidl_fuchsia_io::MAX_BUF as usize);

            std::iter::repeat(hash_bytes()).flatten().take(len).collect()
        }

        let ((), ()) = future::join(
            async {
                for hash in content_blobs() {
                    blobfs.expect_create_blob(hash).await.expect_payload(&payload(hash)).await;
                }
            },
            async {
                let () = stream::iter(content_blobs())
                    .for_each_concurrent(None, |hash| {
                        let (blob, blob_server_end) =
                            fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                        let open_fut =
                            proxy.open_blob(&mut BlobId::from(hash).into(), blob_server_end);

                        async move {
                            assert_matches!(open_fut.await, Ok(Ok(true)));

                            let payload = payload(hash);
                            let _ = blob.truncate(payload.len() as u64).await;
                            let _ = blob.write(&payload).await;
                            let _ = blob.close().await;
                        }
                    })
                    .await;
            },
        )
        .await;

        succeed_poke_pkgfs(&mut pkgfs_install, &mut pkgfs_needs, meta_blob_info).await;

        assert_matches!(task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_many_content_blobs_that_are_already_present() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, mut pkgfs_install, mut pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let content_blobs = || HashRangeFull::default().skip(1).take(100);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, content_blobs()).await;
        enumerate_readable_missing_blobs(&proxy, &mut blobfs, std::iter::empty(), content_blobs())
            .await;

        let ((), ()) = future::join(
            async {
                for hash in content_blobs() {
                    blobfs.expect_create_blob(hash).await.fail_open_with_already_exists();
                    blobfs.expect_open_blob(hash).await.succeed_open_with_blob_readable().await;
                }
            },
            async {
                let () = stream::iter(content_blobs())
                    .for_each(|hash| {
                        let (blob, blob_server_end) =
                            fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                        let open_fut =
                            proxy.open_blob(&mut BlobId::from(hash).into(), blob_server_end);

                        async move {
                            assert_matches!(open_fut.await, Ok(Ok(false)));
                            assert_matches!(blob.take_event_stream().next().await, None);
                        }
                    })
                    .await;
            },
        )
        .await;

        succeed_poke_pkgfs(&mut pkgfs_install, &mut pkgfs_needs, meta_blob_info).await;

        assert_matches!(task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }))
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn allows_retrying_nonfatal_open_blob_errors() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, mut pkgfs_install, mut pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let content_blob = Hash::from([1; 32]);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![content_blob]).await;
        enumerate_readable_missing_blobs(
            &proxy,
            &mut blobfs,
            std::iter::empty(),
            vec![content_blob].into_iter(),
        )
        .await;

        // Try to open the blob, but report it is already being written concurrently.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob(content_blob).await.fail_open_with_already_exists();
                blobfs.expect_open_blob(content_blob).await.fail_open_with_not_readable().await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end).await,
                    Ok(Err(fidl_fuchsia_pkg::OpenBlobError::ConcurrentWrite))
                );
                assert_matches!(blob.truncate(1).await, Err(_));
            },
        )
        .await;

        // Try to write the blob, but report the written contents are corrupt.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob(content_blob).await.fail_write_with_corrupt().await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let _ = blob.truncate(1).await;
                let _ = blob.write(&mut [0]).await;
                let _ = blob.close().await;
            },
        )
        .await;

        // Open the blob for write, but then close it (a non-fatal error)
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob(content_blob).await.expect_close().await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let _ = blob.close().await;
            },
        )
        .await;

        // Operation succeeds after blobfs cooperates.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob(content_blob).await.expect_payload(&[0]).await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let _ = blob.truncate(1).await;
                let _ = blob.write(&mut [0]).await;
                let _ = blob.close().await;
            },
        )
        .await;

        succeed_poke_pkgfs(&mut pkgfs_install, &mut pkgfs_needs, meta_blob_info).await;

        // That was the only data blob, so the operation is now done.
        assert_matches!(task.await, Ok(()));
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn abort_aborts_while_waiting_for_open_meta_blob() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let abort_fut = proxy.abort();

        assert_matches!(task.await, Err(ServeNeededBlobsError::Aborted));
        assert_matches!(
            abort_fut.await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn abort_waits_for_pending_blob_writes_before_responding() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let content_blob = Hash::from([1; 32]);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![content_blob]).await;
        enumerate_readable_missing_blobs(
            &proxy,
            &mut blobfs,
            std::iter::empty(),
            vec![content_blob].into_iter(),
        )
        .await;

        let payload = b"pending blob write";

        let (pending_blob_mock_fut, blob) = future::join(
            async { blobfs.expect_create_blob(content_blob).await.expect_payload(payload) },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<FileMarker>().unwrap();

                let open_fut =
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end);

                async move {
                    assert_matches!(open_fut.await, Ok(Ok(true)));
                }
                .await;
                blob
            },
        )
        .await;

        // abort the operation, expecting abort to complete only after any in-flight blob write
        // operations finish.
        let abort_fut =
            match future::select(proxy.abort(), Timer::new(Duration::from_millis(50))).await {
                Either::Left((r, _)) => panic!("abort future finished early ({:?})!", r),
                Either::Right(((), abort_fut)) => abort_fut,
            };

        // unblock the abort by finishing the pending blob write.
        let ((), ()) = future::join(
            async {
                pending_blob_mock_fut.await;
            },
            async {
                let _ = blob.truncate(payload.len() as u64).await;
                let _ = blob.write(payload).await;
                let _ = blob.close().await;
            },
        )
        .await;

        assert_matches!(
            abort_fut.await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );

        assert_matches!(task.await, Err(ServeNeededBlobsError::Aborted));
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn abort_aborts_while_waiting_for_get_missing_blobs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![]).await;

        let abort_fut = proxy.abort();

        assert_matches!(task.await, Err(ServeNeededBlobsError::Aborted));
        assert_matches!(
            abort_fut.await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn abort_aborts_while_waiting_for_open_blobs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, pkgfs_install, pkgfs_needs, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![[2; 32].into()]).await;
        enumerate_readable_missing_blobs(
            &proxy,
            &mut blobfs,
            std::iter::empty(),
            vec![[2; 32].into()].into_iter(),
        )
        .await;

        let abort_fut = proxy.abort();

        assert_matches!(task.await, Err(ServeNeededBlobsError::Aborted));
        assert_matches!(
            abort_fut.await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
    }
}

#[cfg(test)]
mod get_handler_tests {
    use super::serve_needed_blobs_tests::*;
    use super::*;
    use fidl_fuchsia_pkg::NeededBlobsProxy;
    use fuchsia_cobalt::{CobaltConnector, ConnectionType};
    use fuchsia_inspect as finspect;
    use matches::assert_matches;

    fn spawn_get_with_mocks(
        meta_blob_info: BlobInfo,
        dir_request: Option<ServerEnd<DirectoryMarker>>,
    ) -> (
        Task<Result<(), Status>>,
        NeededBlobsProxy,
        pkgfs::versions::Mock,
        pkgfs::install::Mock,
        pkgfs::needs::Mock,
        blobfs::Mock,
    ) {
        let (proxy, stream) = fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();

        let (pkgfs_versions, pkgfs_versions_mock) = pkgfs::versions::Client::new_mock();
        let (pkgfs_install, pkgfs_install_mock) = pkgfs::install::Client::new_mock();
        let (pkgfs_needs, pkgfs_needs_mock) = pkgfs::needs::Client::new_mock();
        let (blobfs, blobfs_mock) = blobfs::Client::new_mock();
        let inspector = finspect::Inspector::new();
        let package_index = Arc::new(Mutex::new(PackageIndex::new(
            inspector.root().create_child("test_does_not_use_inspect "),
        )));

        let (cobalt_sender, _) =
            CobaltConnector::default().serve(ConnectionType::project_id(metrics::PROJECT_ID));

        (
            Task::spawn(async move {
                get(
                    &pkgfs_versions,
                    &pkgfs_install,
                    &pkgfs_needs,
                    &package_index,
                    &blobfs,
                    meta_blob_info,
                    vec![],
                    stream,
                    dir_request,
                    cobalt_sender,
                    &inspector.root().create_child("test-node-name"),
                )
                .await
            }),
            proxy,
            pkgfs_versions_mock,
            pkgfs_install_mock,
            pkgfs_needs_mock,
            blobfs_mock,
        )
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn everything_closed() {
        let (_, stream) = fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();

        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };

        let (pkgfs_versions, _) = pkgfs::versions::Client::new_test();
        let (pkgfs_install, _) = pkgfs::install::Client::new_test();
        let (pkgfs_needs, _) = pkgfs::needs::Client::new_test();
        let (blobfs, _) = blobfs::Client::new_test();
        let inspector = finspect::Inspector::new();
        let package_index = Arc::new(Mutex::new(PackageIndex::new(
            inspector.root().create_child("test_does_not_use_inspect "),
        )));

        let (cobalt_sender, _) =
            CobaltConnector::default().serve(ConnectionType::project_id(metrics::PROJECT_ID));

        assert_matches!(
            get(
                &pkgfs_versions,
                &pkgfs_install,
                &pkgfs_needs,
                &package_index,
                &blobfs,
                meta_blob_info,
                vec![],
                stream,
                None,
                cobalt_sender,
                &inspector.root().create_child("get")
            )
            .await,
            Err(Status::UNAVAILABLE)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn trivially_opens_present_package() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };

        let (pkgdir, pkgdir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

        let (task, proxy, mut pkgfs_versions, pkgfs_install, pkgfs_needs, blobfs) =
            spawn_get_with_mocks(meta_blob_info, Some(pkgdir_server_end));

        pkgfs_versions
            .expect_open_package([0; 32].into())
            .await
            .succeed_open()
            .await
            .expect_clone()
            .await
            .verify_are_same_channel(pkgdir);
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
        assert_eq!(task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn trivially_opens_present_package_even_if_needed_blobs_is_closed() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };

        let (pkgdir, pkgdir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

        let (task, proxy, mut pkgfs_versions, pkgfs_install, pkgfs_needs, blobfs) =
            spawn_get_with_mocks(meta_blob_info, Some(pkgdir_server_end));

        drop(proxy);

        pkgfs_versions
            .expect_open_package([0; 32].into())
            .await
            .succeed_open()
            .await
            .expect_clone()
            .await
            .verify_are_same_channel(pkgdir);
        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
        assert_eq!(task.await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn opens_missing_package_after_writing_blobs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };

        let (pkgdir, pkgdir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

        let (task, proxy, mut pkgfs_versions, mut pkgfs_install, mut pkgfs_needs, mut blobfs) =
            spawn_get_with_mocks(meta_blob_info, Some(pkgdir_server_end));

        pkgfs_versions.expect_open_package([0; 32].into()).await.fail_open_with_not_found().await;

        write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![]).await;
        enumerate_readable_missing_blobs(
            &proxy,
            &mut blobfs,
            std::iter::empty(),
            std::iter::empty(),
        )
        .await;
        succeed_poke_pkgfs(&mut pkgfs_install, &mut pkgfs_needs, meta_blob_info).await;
        assert_matches!(
            proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. })
        );

        pkgfs_versions
            .expect_open_package([0; 32].into())
            .await
            .succeed_open()
            .await
            .expect_clone()
            .await
            .verify_are_same_channel(pkgdir);

        pkgfs_install.expect_done().await;
        pkgfs_needs.expect_done().await;
        blobfs.expect_done().await;
        assert_eq!(task.await, Ok(()));
    }
}

#[cfg(test)]
mod serve_write_blob_tests {
    use {
        super::*,
        fidl_fuchsia_io::{FileMarker, FileProxy},
        futures::task::Poll,
        matches::assert_matches,
        proptest::prelude::*,
        proptest_derive::Arbitrary,
    };

    /// Calls the provided test function with an open File proxy being served by serve_write_blob
    /// and the corresponding request stream representing the open blobfs file.
    async fn do_serve_write_blob_with<F, Fut>(cb: F) -> Result<(), ServeWriteBlobError>
    where
        F: FnOnce(FileProxy, FileRequestStream) -> Fut,
        Fut: Future<Output = ()>,
    {
        let (blobfs_blob, blobfs_blob_stream) = blobfs::blob::Blob::new_test();

        let (pkg_cache_blob, pkg_cache_blob_stream) =
            fidl::endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let task = serve_write_blob(pkg_cache_blob_stream, blobfs_blob);
        let test = cb(pkg_cache_blob, blobfs_blob_stream);

        let (res, ()) = future::join(task, test).await;
        res
    }

    /// Handles a single FIDL request on the provided stream, panicing if the received request is
    /// not the expected kind.
    macro_rules! serve_fidl_request {
        (
            $stream:expr, { $pat:pat => $handler:block, }
        ) => {
            match $stream.next().await.unwrap().unwrap() {
                $pat => $handler,
                req => panic!("unexpected request: {:?}", req),
            }
        };
    }

    /// Runs the provided FIDL request stream to compleation, running each handler in sequence,
    /// panicing if any incoming request is not the expected kind.
    macro_rules! serve_fidl_stream {
        (
            $stream:expr, { $( $pat:pat => $handler:block, )* }
        ) => {
            async move {
                $(
                    serve_fidl_request!($stream, { $pat => $handler, });
                )*
                assert_matches!($stream.next().await, None);
            }
        }
    }

    /// Sends a truncate request, asserts that the remote end receives the request, responds to the
    /// request, and asserts that the truncate request receives the expected mapped status code.
    async fn verify_truncate(
        proxy: &FileProxy,
        stream: &mut FileRequestStream,
        length: u64,
        blobfs_response: Status,
    ) -> Status {
        let ((), o) = future::join(
            async move {
                serve_fidl_request!(stream, {
                    FileRequest::Truncate { length: actual_length, responder } => {
                        assert_eq!(length, actual_length);
                        responder.send(blobfs_response.into_raw()).unwrap();
                    },
                });

                // Also expect the client to close the blob on truncate error.
                if blobfs_response != Status::OK {
                    serve_fidl_request!(stream, {
                        FileRequest::Close { responder } => {
                            responder.send(Status::OK.into_raw()).unwrap();
                        },
                    });
                }
            },
            async move {
                let s = proxy.truncate(length).await.map(Status::from_raw).unwrap();

                s
            },
        )
        .await;
        o
    }

    /// Sends a write request, asserts that the remote end receives the request, responds to the
    /// request, and asserts that the write request receives the expected mapped status code/length.
    async fn verify_write(
        proxy: &FileProxy,
        stream: &mut FileRequestStream,
        data: &[u8],
        blobfs_response: Status,
    ) -> Status {
        let ((), o) = future::join(
            async move {
                serve_fidl_request!(stream, {
                    FileRequest::Write{ data: actual_data, responder } => {
                        assert_eq!(data, actual_data);
                        responder.send(blobfs_response.into_raw(), data.len() as u64).unwrap();
                    },
                });

                // Also expect the client to close the blob on write error.
                if blobfs_response != Status::OK {
                    serve_fidl_request!(stream, {
                        FileRequest::Close { responder } => {
                            responder.send(Status::OK.into_raw()).unwrap();
                        },
                    });
                }
            },
            async move {
                let (s, len) =
                    proxy.write(data).await.map(|(s, len)| (Status::from_raw(s), len)).unwrap();
                if s == Status::OK {
                    assert_eq!(len, data.len() as u64);
                }
                s
            },
        )
        .await;
        o
    }

    /// Verify that closing the proxy results in the pkgfs backing file being explicitly closed.
    async fn verify_inner_blob_closes(proxy: FileProxy, mut stream: FileRequestStream) {
        drop(proxy);
        serve_fidl_stream!(stream, {
            FileRequest::Close { responder } => {
                responder.send(Status::OK.into_raw()).unwrap();
            },
        })
        .await;
    }

    /// Verify that an explicit close() request is proxied through to the pkgfs backing file.
    async fn verify_explicit_close(proxy: FileProxy, mut stream: FileRequestStream) {
        let ((), ()) = future::join(
            serve_fidl_stream!(stream, {
                FileRequest::Close { responder } => {
                    responder.send(Status::OK.into_raw()).unwrap();
                },
            }),
            async move {
                let _ = proxy.close().await;
                drop(proxy);
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_stop() {
        let res = do_serve_write_blob_with(|proxy, stream| async move {
            drop(proxy);
            drop(stream);
        })
        .await;
        assert_matches!(res, Err(ServeWriteBlobError::UnexpectedClose));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn happy_path_succeeds() {
        do_serve_write_blob_with(|proxy, mut stream| async move {
            assert_eq!(verify_truncate(&proxy, &mut stream, 200, Status::OK).await, Status::OK);
            assert_eq!(verify_write(&proxy, &mut stream, &[1; 100], Status::OK).await, Status::OK);
            assert_eq!(verify_write(&proxy, &mut stream, &[2; 100], Status::OK).await, Status::OK);
            verify_explicit_close(proxy, stream).await;
        })
        .await
        .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn happy_path_implicit_close_succeeds() {
        do_serve_write_blob_with(|proxy, mut stream| async move {
            assert_eq!(verify_truncate(&proxy, &mut stream, 200, Status::OK).await, Status::OK);
            assert_eq!(verify_write(&proxy, &mut stream, &[1; 100], Status::OK).await, Status::OK);
            assert_eq!(verify_write(&proxy, &mut stream, &[2; 100], Status::OK).await, Status::OK);
            verify_inner_blob_closes(proxy, stream).await;
        })
        .await
        .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn raises_out_of_space_during_truncate() {
        let res = do_serve_write_blob_with(|proxy, mut stream| async move {
            assert_eq!(
                verify_truncate(&proxy, &mut stream, 100, Status::NO_SPACE).await,
                Status::NO_SPACE
            );
            verify_inner_blob_closes(proxy, stream).await;
        })
        .await;
        assert_matches!(res, Err(ServeWriteBlobError::NoSpace));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn raises_corrupt_during_truncate() {
        // only for the empty blob
        let res = do_serve_write_blob_with(|proxy, mut stream| async move {
            assert_eq!(
                verify_truncate(&proxy, &mut stream, 0, Status::IO_DATA_INTEGRITY).await,
                Status::IO_DATA_INTEGRITY
            );
            verify_inner_blob_closes(proxy, stream).await;
        })
        .await;
        assert_matches!(res, Err(ServeWriteBlobError::Corrupt));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn truncate_maps_unknown_errors_to_internal() {
        let res = do_serve_write_blob_with(|proxy, mut stream| async move {
            assert_eq!(
                verify_truncate(&proxy, &mut stream, 100, Status::ADDRESS_UNREACHABLE).await,
                Status::INTERNAL
            );
            verify_inner_blob_closes(proxy, stream).await;
        })
        .await;
        assert_matches!(res, Err(ServeWriteBlobError::Truncate(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn raises_out_of_space_during_write() {
        let res = do_serve_write_blob_with(|proxy, mut stream| async move {
            assert_eq!(verify_truncate(&proxy, &mut stream, 100, Status::OK).await, Status::OK);
            assert_eq!(
                verify_write(&proxy, &mut stream, &[0; 1], Status::NO_SPACE).await,
                Status::NO_SPACE
            );
            verify_inner_blob_closes(proxy, stream).await;
        })
        .await;
        assert_matches!(res, Err(ServeWriteBlobError::NoSpace));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn raises_corrupt_during_last_write() {
        let res = do_serve_write_blob_with(|proxy, mut stream| async move {
            assert_eq!(verify_truncate(&proxy, &mut stream, 10, Status::OK).await, Status::OK);
            assert_eq!(verify_write(&proxy, &mut stream, &[0; 5], Status::OK).await, Status::OK);
            assert_eq!(
                verify_write(&proxy, &mut stream, &[1; 5], Status::IO_DATA_INTEGRITY).await,
                Status::IO_DATA_INTEGRITY
            );
            verify_inner_blob_closes(proxy, stream).await;
        })
        .await;
        assert_matches!(res, Err(ServeWriteBlobError::Corrupt));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_maps_unknown_errors_to_internal() {
        let res = do_serve_write_blob_with(|proxy, mut stream| async move {
            assert_eq!(verify_truncate(&proxy, &mut stream, 100, Status::OK).await, Status::OK);
            assert_eq!(
                verify_write(&proxy, &mut stream, &[1; 1], Status::ADDRESS_UNREACHABLE).await,
                Status::INTERNAL
            );
            verify_inner_blob_closes(proxy, stream).await;
        })
        .await;
        assert_matches!(res, Err(ServeWriteBlobError::Write(_)));
    }

    #[test]
    fn close_closes_inner_blob_first() {
        let mut executor = fuchsia_async::TestExecutor::new().unwrap();

        let (blobfs_blob, mut blobfs_blob_stream) = blobfs::blob::Blob::new_test();

        let (pkg_cache_blob, pkg_cache_blob_stream) =
            fidl::endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let task = serve_write_blob(pkg_cache_blob_stream, blobfs_blob);
        futures::pin_mut!(task);

        let mut close_fut = pkg_cache_blob.close();
        drop(pkg_cache_blob);

        // Let the task process the close request, ensuring the close_future doesn't yet complete.
        assert_matches!(executor.run_until_stalled(&mut task), Poll::Pending);
        assert_matches!(executor.run_until_stalled(&mut close_fut), Poll::Pending);

        // Verify the inner blob is bineg closed.
        let () = executor.run_singlethreaded(async {
            serve_fidl_request!(blobfs_blob_stream, {
                FileRequest::Close { responder } => {
                    responder.send(Status::OK.into_raw()).unwrap();
                },
            })
        });

        // Now that the inner blob is closed, the proxy task and close request can complete
        assert_matches!(
            executor.run_until_stalled(&mut task),
            Poll::Ready(Err(ServeWriteBlobError::UnexpectedClose))
        );
        assert_matches!(executor.run_until_stalled(&mut close_fut), Poll::Ready(Ok(0)));
    }

    #[derive(Debug, Clone, Copy, PartialEq, Eq, Arbitrary)]
    enum StubRequestor {
        Clone,
        Describe,
        Sync,
        GetAttr,
        SetAttr,
        NodeGetFlags,
        NodeSetFlags,
        Write,
        WriteAt,
        Read,
        ReadAt,
        Seek,
        Truncate,
        GetFlags,
        SetFlags,
        GetBuffer,
        // New API that references fuchsia.io2. Not strictly necessary to verify all possible
        // ordinals (which is the space of a u64 anyway).
        // AdvisoryLock

        // Always allowed.
        // Close
    }

    impl StubRequestor {
        fn method_name(self) -> &'static str {
            match self {
                StubRequestor::Clone => "clone",
                StubRequestor::Describe => "describe",
                StubRequestor::Sync => "sync",
                StubRequestor::GetAttr => "get_attr",
                StubRequestor::SetAttr => "set_attr",
                StubRequestor::NodeGetFlags => "node_get_flags",
                StubRequestor::NodeSetFlags => "node_set_flags",
                StubRequestor::Write => "write",
                StubRequestor::WriteAt => "write_at",
                StubRequestor::Read => "read",
                StubRequestor::ReadAt => "read_at",
                StubRequestor::Seek => "seek",
                StubRequestor::Truncate => "truncate",
                StubRequestor::GetFlags => "get_flags",
                StubRequestor::SetFlags => "set_flags",
                StubRequestor::GetBuffer => "get_buffer",
            }
        }

        fn make_stub_request(self, proxy: &FileProxy) -> impl Future<Output = ()> {
            use fidl::encoding::Decodable;
            match self {
                StubRequestor::Clone => {
                    let (_, server_end) =
                        fidl::endpoints::create_proxy::<fidl_fuchsia_io::NodeMarker>().unwrap();
                    let _ = proxy.clone(0, server_end);
                    future::ready(()).boxed()
                }
                StubRequestor::Describe => proxy.describe().map(|_| ()).boxed(),
                StubRequestor::Sync => proxy.sync().map(|_| ()).boxed(),
                StubRequestor::GetAttr => proxy.get_attr().map(|_| ()).boxed(),
                StubRequestor::SetAttr => proxy
                    .set_attr(0, &mut fidl_fuchsia_io::NodeAttributes::new_empty())
                    .map(|_| ())
                    .boxed(),
                StubRequestor::NodeGetFlags => proxy.node_get_flags().map(|_| ()).boxed(),
                StubRequestor::NodeSetFlags => proxy.node_set_flags(0).map(|_| ()).boxed(),
                StubRequestor::Write => proxy.write(&[0; 0]).map(|_| ()).boxed(),
                StubRequestor::WriteAt => proxy.write_at(&[0; 0], 0).map(|_| ()).boxed(),
                StubRequestor::Read => proxy.read(0).map(|_| ()).boxed(),
                StubRequestor::ReadAt => proxy.read_at(0, 0).map(|_| ()).boxed(),
                StubRequestor::Seek => {
                    proxy.seek(0, fidl_fuchsia_io::SeekOrigin::Start).map(|_| ()).boxed()
                }
                StubRequestor::Truncate => proxy.truncate(0).map(|_| ()).boxed(),
                StubRequestor::GetFlags => proxy.get_flags().map(|_| ()).boxed(),
                StubRequestor::SetFlags => proxy.set_flags(0).map(|_| ()).boxed(),
                StubRequestor::GetBuffer => proxy.get_buffer(0).map(|_| ()).boxed(),
            }
        }
    }

    #[derive(Debug, Clone, Copy, PartialEq, Eq, Arbitrary)]
    enum InitialState {
        ExpectTruncate,
        ExpectWrite,
        ExpectClose,
    }

    impl InitialState {
        fn expected_method_name(self) -> &'static str {
            match self {
                InitialState::ExpectTruncate => "truncate",
                InitialState::ExpectWrite => "write",
                InitialState::ExpectClose => "close",
            }
        }

        async fn enter(self, proxy: &FileProxy, stream: &mut FileRequestStream) {
            match self {
                InitialState::ExpectTruncate => {}
                InitialState::ExpectWrite => {
                    assert_eq!(verify_truncate(proxy, stream, 100, Status::OK).await, Status::OK);
                }
                InitialState::ExpectClose => {
                    assert_eq!(verify_truncate(proxy, stream, 100, Status::OK).await, Status::OK);
                    assert_eq!(
                        verify_write(proxy, stream, &[0; 100], Status::OK).await,
                        Status::OK
                    );
                }
            }
        }
    }

    proptest! {
        // Failure seed persistence isn't working in Fuchsia tests, and these tests are expected to
        // verify the entire input space anyway. Enable result caching to skip running the same
        // case more than once.
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            result_cache: proptest::test_runner::basic_result_cache,
            ..Default::default()
        })]

        #[test]
        fn allows_close_in_any_state(initial_state: InitialState) {
            let mut executor = fuchsia_async::TestExecutor::new().unwrap();
            let () = executor.run_singlethreaded(async move {

                let res = do_serve_write_blob_with(|proxy, mut stream| async move {
                    initial_state.enter(&proxy, &mut stream).await;
                    verify_explicit_close(proxy, stream).await;
                })
                .await;

                match initial_state {
                    InitialState::ExpectClose => assert_matches!(res, Ok(())),
                    _ => assert_matches!(res, Err(ServeWriteBlobError::UnexpectedClose)),
                }
            });
        }

        #[test]
        fn rejects_unexpected_requests(initial_state: InitialState, bad_request: StubRequestor) {
            // Skip stub requests that are the expected request for this initial state.
            prop_assume!(initial_state.expected_method_name() != bad_request.method_name());

            let mut executor = fuchsia_async::TestExecutor::new().unwrap();
            let () = executor.run_singlethreaded(async move {

                let res = do_serve_write_blob_with(|proxy, mut stream| async move {
                    initial_state.enter(&proxy, &mut stream).await;

                    let bad_request_fut = bad_request.make_stub_request(&proxy);

                    let ((), ()) = future::join(
                        async move {
                            let _ = bad_request_fut.await;
                        },
                        verify_inner_blob_closes(proxy, stream),
                    )
                    .await;
                })
                .await;

                match res {
                    Err(ServeWriteBlobError::UnexpectedRequest{ received, expected }) => {
                        prop_assert_eq!(received, bad_request.method_name());
                        prop_assert_eq!(expected, initial_state.expected_method_name());
                    }
                    res => panic!("Expected UnexpectedRequest error, got {:?}", res),
                }
                Ok(())
            })?;
        }
    }
}

#[cfg(test)]
mod serve_base_package_index_tests {
    use {super::*, fidl_fuchsia_pkg::PackageIndexIteratorMarker, fuchsia_pkg::PackagePath};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn static_packages_entries_converted_correctly() {
        let static_packages = StaticPackages::from_entries(vec![
            (PackagePath::from_name_and_variant("name0", "0").unwrap(), Hash::from([0u8; 32])),
            (PackagePath::from_name_and_variant("name1", "1").unwrap(), Hash::from([1u8; 32])),
        ]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PackageIndexIteratorMarker>().unwrap();
        let task = Task::local(serve_base_package_index(Arc::new(static_packages), stream));

        let entries = proxy.next().await.unwrap();
        assert_eq!(
            entries,
            vec![
                fidl_fuchsia_pkg::PackageIndexEntry {
                    package_url: fidl_fuchsia_pkg::PackageUrl {
                        url: "fuchsia-pkg://fuchsia.com/name0".to_string()
                    },
                    meta_far_blob_id: fidl_fuchsia_pkg::BlobId { merkle_root: [0u8; 32] }
                },
                fidl_fuchsia_pkg::PackageIndexEntry {
                    package_url: fidl_fuchsia_pkg::PackageUrl {
                        url: "fuchsia-pkg://fuchsia.com/name1".to_string()
                    },
                    meta_far_blob_id: fidl_fuchsia_pkg::BlobId { merkle_root: [1u8; 32] }
                }
            ]
        );

        let entries = proxy.next().await.unwrap();
        assert_eq!(entries, vec![]);

        let () = task.await;
    }
}
