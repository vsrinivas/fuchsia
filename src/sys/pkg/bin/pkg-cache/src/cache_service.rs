// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::base_packages::BasePackages,
    crate::index::{
        fulfill_meta_far_blob, CompleteInstallError, FulfillMetaFarError, PackageIndex,
    },
    anyhow::{anyhow, Error},
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::ServerEnd,
    fidl::prelude::*,
    fidl_contrib::protocol_connector::ProtocolSender,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_metrics::MetricEvent,
    fidl_fuchsia_pkg::{
        self as fpkg, NeededBlobsMarker, NeededBlobsRequest, NeededBlobsRequestStream,
        PackageCacheRequest, PackageCacheRequestStream, PackageIndexEntry,
        PackageIndexIteratorRequestStream,
    },
    fidl_fuchsia_pkg_ext::{
        serve_fidl_iterator_from_slice, serve_fidl_iterator_from_stream, BlobId, BlobInfo,
    },
    fuchsia_async::Task,
    fuchsia_cobalt_builders::MetricEventExt,
    fuchsia_hash::Hash,
    fuchsia_inspect::{self as finspect, NumericProperty, Property, StringProperty},
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_trace as ftrace, fuchsia_zircon as zx,
    fuchsia_zircon::Status,
    futures::{prelude::*, select_biased, stream::FuturesUnordered},
    std::sync::{
        atomic::{AtomicU32, Ordering},
        Arc,
    },
    vfs::directory::entry::DirectoryEntry as _,
};

mod missing_blobs;

// This encodes a host to interpolate when responding to BasePackageIndex requests.
const BASE_PACKAGE_HOST: &str = "fuchsia.com";

pub(crate) async fn serve(
    package_index: Arc<async_lock::RwLock<PackageIndex>>,
    blobfs: blobfs::Client,
    base_packages: Arc<BasePackages>,
    cache_packages: Arc<Option<system_image::CachePackages>>,
    executability_restrictions: system_image::ExecutabilityRestrictions,
    non_static_allow_list: Arc<system_image::NonStaticAllowList>,
    subpackages_config: crate::SubpackagesConfig,
    scope: package_directory::ExecutionScope,
    stream: PackageCacheRequestStream,
    cobalt_sender: ProtocolSender<MetricEvent>,
    serve_id: Arc<AtomicU32>,
    get_node: Arc<finspect::Node>,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| async {
            let cobalt_sender = cobalt_sender.clone();
            match event {
                PackageCacheRequest::Get { meta_far_blob, needed_blobs, dir, responder } => {
                    let id = serve_id.fetch_add(1, Ordering::SeqCst);
                    let meta_far_blob: BlobInfo = meta_far_blob.into();
                    let node = get_node.create_child(id.to_string());
                    let trace_id = ftrace::Id::random();
                    let guard = ftrace::async_enter!(
                        trace_id,
                        "app",
                        "cache_get",
                        "meta_far_blob_id" => meta_far_blob.blob_id.to_string().as_str(),
                        // An async duration cannot have multiple concurrent child async durations
                        // so we include the id as metadata to manually determine the
                        // relationship.
                        "trace_id" => u64::from(trace_id)
                    );
                    let response = get(
                        package_index.as_ref(),
                        base_packages.as_ref(),
                        executability_restrictions,
                        non_static_allow_list.as_ref(),
                        &blobfs,
                        meta_far_blob,
                        needed_blobs,
                        dir.map(|dir| (dir, scope.clone())),
                        subpackages_config,
                        cobalt_sender,
                        &node,
                        trace_id,
                    )
                    .await;
                    guard.end(&[ftrace::ArgValue::of(
                        "status",
                        Status::from(response).to_string().as_str(),
                    )]);
                    drop(node);
                    responder.send(&mut response.map_err(|status| status.into_raw()))?;
                }
                PackageCacheRequest::Open { meta_far_blob_id, dir, responder } => {
                    let meta_far: Hash = BlobId::from(meta_far_blob_id).into();
                    let trace_id = ftrace::Id::random();
                    let guard = ftrace::async_enter!(
                        trace_id,
                        "app",
                        "cache_open",
                        "meta_far_blob_id" => meta_far.to_string().as_str()
                    );
                    let response = open(
                        package_index.as_ref(),
                        base_packages.as_ref(),
                        executability_restrictions,
                        non_static_allow_list.as_ref(),
                        scope.clone(),
                        &blobfs,
                        meta_far,
                        dir,
                        cobalt_sender,
                    )
                    .await;
                    guard.end(&[ftrace::ArgValue::of(
                        "status",
                        Status::from(response).to_string().as_str(),
                    )]);
                    responder.send(&mut response.map_err(|status| status.into_raw()))?;
                }
                PackageCacheRequest::BasePackageIndex { iterator, control_handle: _ } => {
                    let stream = iterator.into_stream()?;
                    serve_base_package_index(BASE_PACKAGE_HOST, Arc::clone(&base_packages), stream)
                        .await;
                }
                PackageCacheRequest::CachePackageIndex { iterator, control_handle: _ } => {
                    let stream = iterator.into_stream()?;
                    serve_cache_package_index(Arc::clone(&cache_packages), stream).await;
                }
                PackageCacheRequest::Sync { responder } => {
                    responder.send(&mut blobfs.sync().await.map_err(|e| {
                        fx_log_err!("error syncing blobfs: {:#}", anyhow!(e));
                        Status::INTERNAL.into_raw()
                    }))?;
                }
            }

            Ok(())
        })
        .await
}

pub(crate) enum PackageStatus {
    Base,
    Active(fuchsia_pkg::PackageName),
    Other,
}

pub(crate) async fn get_package_status(
    base_packages: &BasePackages,
    package_index: &async_lock::RwLock<PackageIndex>,
    package: &fuchsia_hash::Hash,
) -> PackageStatus {
    if base_packages.is_base_package(*package) {
        return PackageStatus::Base;
    }

    match package_index.read().await.get_name_if_active(package) {
        Some(name) => PackageStatus::Active(name.clone()),
        None => PackageStatus::Other,
    }
}

pub(crate) enum ExecutabilityStatus {
    Allowed,
    Forbidden,
}

pub(crate) fn executability_status(
    executability_restrictions: system_image::ExecutabilityRestrictions,
    package_status: &PackageStatus,
    non_static_allow_list: &system_image::NonStaticAllowList,
) -> ExecutabilityStatus {
    use {system_image::ExecutabilityRestrictions::*, ExecutabilityStatus::*, PackageStatus::*};
    match (executability_restrictions, package_status) {
        (Enforce, Base) => Allowed,
        (Enforce, Active(name)) => {
            if non_static_allow_list.allows(name) {
                Allowed
            } else {
                Forbidden
            }
        }
        (Enforce, Other) => Forbidden,
        (DoNotEnforce, _) => Allowed,
    }
}

fn make_pkgdir_flags(executability_status: ExecutabilityStatus) -> fio::OpenFlags {
    use ExecutabilityStatus::*;
    fio::OpenFlags::RIGHT_READABLE
        | match executability_status {
            Allowed => fio::OpenFlags::RIGHT_EXECUTABLE,
            Forbidden => fio::OpenFlags::empty(),
        }
}

/// Fetch a package, and optionally open it.
async fn get(
    package_index: &async_lock::RwLock<PackageIndex>,
    base_packages: &BasePackages,
    executability_restrictions: system_image::ExecutabilityRestrictions,
    non_static_allow_list: &system_image::NonStaticAllowList,
    blobfs: &blobfs::Client,
    meta_far_blob: BlobInfo,
    needed_blobs: ServerEnd<NeededBlobsMarker>,
    dir_and_scope: Option<(ServerEnd<fio::DirectoryMarker>, package_directory::ExecutionScope)>,
    subpackages_config: crate::SubpackagesConfig,
    mut cobalt_sender: ProtocolSender<MetricEvent>,
    node: &finspect::Node,
    trace_id: ftrace::Id,
) -> Result<(), Status> {
    let _time_prop = node.create_int("started-time", zx::Time::get_monotonic().into_nanos());
    let _id_prop = node.create_string("meta-far-id", meta_far_blob.blob_id.to_string());
    let _length_prop = node.create_uint("meta-far-length", meta_far_blob.length);

    let needed_blobs = needed_blobs.into_stream().map_err(|_| Status::INTERNAL)?;

    let (root_dir, package_status) =
        match get_package_status(base_packages, package_index, &meta_far_blob.blob_id.into()).await
        {
            ps @ PackageStatus::Base | ps @ PackageStatus::Active(_) => {
                let () = needed_blobs.control_handle().shutdown_with_epitaph(Status::OK);
                (None, ps)
            }
            PackageStatus::Other => {
                fx_log_info!("get package {}", meta_far_blob.blob_id);
                let (root_dir, name) = serve_needed_blobs(
                    needed_blobs,
                    meta_far_blob,
                    package_index,
                    blobfs,
                    subpackages_config,
                    node,
                    trace_id,
                )
                .await
                .map_err(|e| {
                    fx_log_err!(
                        "error while caching package {}: {:#}",
                        meta_far_blob.blob_id,
                        anyhow!(e)
                    );
                    cobalt_sender.send(
                        MetricEvent::builder(metrics::PKG_CACHE_OPEN_MIGRATED_METRIC_ID)
                            .with_event_codes(
                                metrics::PkgCacheOpenMigratedMetricDimensionResult::Io,
                            )
                            .as_occurrence(1),
                    );
                    Status::UNAVAILABLE
                })?;
                let package_status = if let Some(name) = name {
                    PackageStatus::Active(name)
                } else {
                    PackageStatus::Other
                };
                (Some(root_dir), package_status)
            }
        };

    if let Some((dir, scope)) = dir_and_scope {
        let root_dir = if let Some(root_dir) = root_dir {
            root_dir
        } else {
            package_directory::RootDir::new(blobfs.clone(), meta_far_blob.blob_id.into())
                .await
                .map_err(|e| {
                    fx_log_err!(
                        "get: creating RootDir {}: {:#}",
                        meta_far_blob.blob_id,
                        anyhow!(e)
                    );
                    cobalt_sender.send(
                        MetricEvent::builder(metrics::PKG_CACHE_OPEN_MIGRATED_METRIC_ID)
                            .with_event_codes(
                                metrics::PkgCacheOpenMigratedMetricDimensionResult::Io,
                            )
                            .as_occurrence(1),
                    );
                    Status::INTERNAL
                })?
        };
        let () = Arc::new(root_dir).open(
            scope,
            make_pkgdir_flags(executability_status(
                executability_restrictions,
                &package_status,
                non_static_allow_list,
            )),
            0,
            vfs::path::Path::dot(),
            dir.into_channel().into(),
        );
    }

    cobalt_sender.send(
        MetricEvent::builder(metrics::PKG_CACHE_OPEN_MIGRATED_METRIC_ID)
            .with_event_codes(metrics::PkgCacheOpenMigratedMetricDimensionResult::Success)
            .as_occurrence(1),
    );
    Ok(())
}

/// Open a package directory.
async fn open(
    package_index: &async_lock::RwLock<PackageIndex>,
    base_packages: &BasePackages,
    executability_restrictions: system_image::ExecutabilityRestrictions,
    non_static_allow_list: &system_image::NonStaticAllowList,
    scope: package_directory::ExecutionScope,
    blobfs: &blobfs::Client,
    meta_far: Hash,
    dir_request: ServerEnd<fio::DirectoryMarker>,
    mut cobalt_sender: ProtocolSender<MetricEvent>,
) -> Result<(), Status> {
    let package_status = match get_package_status(base_packages, package_index, &meta_far).await {
        PackageStatus::Other => {
            cobalt_sender.send(
                MetricEvent::builder(metrics::PKG_CACHE_OPEN_MIGRATED_METRIC_ID)
                    .with_event_codes(metrics::PkgCacheOpenMigratedMetricDimensionResult::NotFound)
                    .as_occurrence(1),
            );
            return Err(Status::NOT_FOUND);
        }
        ps @ PackageStatus::Base | ps @ PackageStatus::Active(_) => ps,
    };

    let () = package_directory::serve(
        scope,
        blobfs.clone(),
        meta_far,
        make_pkgdir_flags(executability_status(
            executability_restrictions,
            &package_status,
            non_static_allow_list,
        )),
        dir_request,
    )
    .map_err(|e| {
        fx_log_err!("open: error serving package {}: {:#}", meta_far, anyhow!(e));
        cobalt_sender.send(
            MetricEvent::builder(metrics::PKG_CACHE_OPEN_MIGRATED_METRIC_ID)
                .with_event_codes(metrics::PkgCacheOpenMigratedMetricDimensionResult::Io)
                .as_occurrence(1),
        );
        Status::INTERNAL
    })
    .await?;

    cobalt_sender.send(
        MetricEvent::builder(metrics::PKG_CACHE_OPEN_MIGRATED_METRIC_ID)
            .with_event_codes(metrics::PkgCacheOpenMigratedMetricDimensionResult::Success)
            .as_occurrence(1),
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

    #[error("while adding needed content blobs to the iterator")]
    SendNeededContentBlobs(#[source] futures::channel::mpsc::SendError),

    #[error("while adding needed subpackage meta.fars to the iterator")]
    SendNeededSubpackageBlobs(#[source] futures::channel::mpsc::SendError),

    #[error("while creating a RootDir for a subpackage")]
    CreateSubpackageRootDir(#[source] package_directory::Error),

    #[error("while reading the subpackages of a package")]
    ReadSubpackages(#[source] package_directory::SubpackagesError),

    #[error(
        "handle_open_blobs finished writing all the needed blobs but still had {count} \
             outstanding blob write futures. This should be impossible"
    )]
    OutstandingBlobWritesWhenHandleOpenBlobsFinished { count: usize },
}

#[derive(Debug)]
enum BlobKind {
    Package,
    Data,
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
///
/// Returns the package's name if the package was activated in the dynamic index.
async fn serve_needed_blobs(
    mut stream: NeededBlobsRequestStream,
    meta_far_info: BlobInfo,
    package_index: &async_lock::RwLock<PackageIndex>,
    blobfs: &blobfs::Client,
    subpackages_config: crate::SubpackagesConfig,
    node: &finspect::Node,
    trace_id: ftrace::Id,
) -> Result<
    (package_directory::RootDir<blobfs::Client>, Option<fuchsia_pkg::PackageName>),
    ServeNeededBlobsError,
> {
    let state = node.create_string("state", "need-meta-far");
    let res = async {
        // Step 1: Open and write the meta.far, or determine it is not needed.
        let root_dir = handle_open_meta_blob(
            &mut stream,
            meta_far_info,
            blobfs,
            package_index,
            &state,
            trace_id,
        )
        .await?;

        let (missing_blobs, missing_blobs_recv) =
            missing_blobs::MissingBlobs::new(blobfs.clone(), subpackages_config, &root_dir).await?;

        // Step 2: Determine which data blobs are needed and report them to the client.
        let serve_iterator = handle_get_missing_blobs(&mut stream, missing_blobs_recv).await?;

        state.set("need-content-blobs");

        // Step 3: Open and write all needed data blobs.
        let () = handle_open_blobs(&mut stream, missing_blobs, blobfs, &node, trace_id).await?;

        let () = serve_iterator.await;
        Ok(root_dir)
    }
    .await;

    let res = match res {
        Ok(root_dir) => Ok((
            root_dir,
            package_index.write().await.complete_install(meta_far_info.blob_id.into())?,
        )),
        Err(e) => {
            package_index.write().await.cancel_install(&meta_far_info.blob_id.into());
            Err(e)
        }
    };

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
    package_index: &async_lock::RwLock<PackageIndex>,
    state: &StringProperty,
    trace_id: ftrace::Id,
) -> Result<package_directory::RootDir<blobfs::Client>, ServeNeededBlobsError> {
    let hash = meta_far_info.blob_id.into();
    package_index.write().await.start_install(hash);

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

        match open_write_blob(file_stream, responder, blobfs, hash, BlobKind::Package, trace_id)
            .await
        {
            Ok(()) => break,
            Err(OpenWriteBlobError::Serve(e)) => return Err(e),
            Err(OpenWriteBlobError::NonFatalWrite(e)) => {
                fx_log_warn!("Non-fatal error while writing metadata blob: {:#}", anyhow!(e));
                continue;
            }
        }
    }

    state.set("enumerate-missing-blobs");

    Ok(fulfill_meta_far_blob(package_index, blobfs, hash).await?)
}

async fn handle_get_missing_blobs(
    stream: &mut NeededBlobsRequestStream,
    missing_blobs: futures::channel::mpsc::UnboundedReceiver<Vec<fidl_fuchsia_pkg::BlobInfo>>,
) -> Result<Task<()>, ServeNeededBlobsError> {
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

    // Start serving the iterator in the background and internally move on to the next state. If
    // this foreground task decides to bail out, this spawned task will be dropped which will abort
    // the iterator serving task.
    Ok(Task::spawn(
        serve_fidl_iterator_from_stream(
            iter_stream,
            missing_blobs,
            // Unlikely that more than 10 Vec<BlobInfo> (e.g. 5 RootDirs with subpackages)
            // will be written to missing_blobs between calls to Iterator::Next by the FIDL client,
            // so no need to increase this which would use (a tiny amount) more memory.
            10,
        )
        .unwrap_or_else(|e| {
            fx_log_err!("error serving BlobInfoIteratorRequestStream: {:#}", anyhow!(e))
        }),
    ))
}

async fn handle_open_blobs(
    stream: &mut NeededBlobsRequestStream,
    mut missing_blobs: missing_blobs::MissingBlobs,
    blobfs: &blobfs::Client,
    node: &finspect::Node,
    trace_id: ftrace::Id,
) -> Result<(), ServeNeededBlobsError> {
    let mut writing = FuturesUnordered::new();

    let known_remaining_counter =
        node.create_uint("known_remaining", missing_blobs.count_not_cached() as u64);
    let writing_counter = node.create_uint("writing", 0);
    let written_counter = node.create_uint("written", 0);

    while missing_blobs.count_not_cached() != 0 {
        #[derive(Debug)]
        enum Event {
            WriteBlobDone((Hash, Result<(), OpenWriteBlobError>)),
            Request(Option<NeededBlobsRequest>),
        }

        // Wait for the next request/event to happen, giving priority to handling blob write
        // completion events to new incoming requests.
        let event = select_biased! {
            res = writing.select_next_some() => {
                writing_counter.set(writing.len() as u64);
                Event::WriteBlobDone(res)
            }
            req = stream.try_next() =>
                Event::Request(req.map_err(ServeNeededBlobsError::ReceiveRequest)?),
        };

        match event {
            Event::Request(Some(NeededBlobsRequest::OpenBlob { blob_id, file, responder })) => {
                let blob_id = Hash::from(BlobId::from(blob_id));

                if !missing_blobs.should_cache(&blob_id) {
                    return Err(ServeNeededBlobsError::BlobNotNeeded(blob_id));
                }

                let file_stream =
                    file.into_stream().map_err(ServeNeededBlobsError::ReceiveRequest)?;

                // Do the actual async work of opening the blob for write and serving the write
                // calls in a separate Future so this loop can run this Future and handle new
                // requests concurrently.
                let task = open_write_blob(
                    file_stream,
                    responder,
                    blobfs,
                    blob_id,
                    BlobKind::Data,
                    trace_id,
                );
                writing.push(async move { (blob_id, task.await) });
                writing_counter.set(writing.len() as u64);
                continue;
            }
            Event::Request(Some(NeededBlobsRequest::Abort { responder })) => {
                // Finish all currently open blobs before aborting.
                while !writing.is_empty() {
                    writing.next().await;
                    writing_counter.set(writing.len() as u64);
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
            Event::WriteBlobDone((hash, Ok(()))) => {
                let () = missing_blobs.cache(&hash).await?;
                known_remaining_counter.set(missing_blobs.count_not_cached() as u64);
                written_counter.add(1);
                continue;
            }
            Event::WriteBlobDone((_, Err(OpenWriteBlobError::Serve(e)))) => {
                return Err(e);
            }
            Event::WriteBlobDone((hash, Err(OpenWriteBlobError::NonFatalWrite(e)))) => {
                fx_log_warn!(
                    "Non-fatal error while writing content blob: {} {:#}",
                    hash,
                    anyhow!(e)
                );
                continue;
            }
        }
    }

    if !writing.is_empty() {
        Err(ServeNeededBlobsError::OutstandingBlobWritesWhenHandleOpenBlobsFinished {
            count: writing.len(),
        })
    } else {
        Ok(())
    }
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
    file_stream: fio::FileRequestStream,
    responder: impl OpenBlobResponder,
    blobfs: &blobfs::Client,
    blob_id: Hash,
    kind: BlobKind,
    parent_trace_id: ftrace::Id,
) -> Result<(), OpenWriteBlobError> {
    let trace_id = ftrace::Id::random();
    let _guard = ftrace::async_enter!(
        trace_id,
        "app",
        "open_write_blob",
        "blob" => blob_id.to_string().as_str(),
        // An async duration cannot have multiple concurrent child async durations
        // so we include the nonce as metadata to manually determine the
        // relationship.
        "parent_trace_id" => u64::from(parent_trace_id)
    );
    let create_res = blobfs.open_blob_for_write(&blob_id).await;
    ftrace::async_instant!(trace_id, "app", "open_write_blob_opened");

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
        Ok(blob) => serve_write_blob(file_stream, blob, trace_id).await.map_err(|e| {
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
    mut stream: fio::FileRequestStream,
    blob: blobfs::blob::Blob<blobfs::blob::NeedsTruncate>,
    trace_id: ftrace::Id,
) -> Result<(), ServeWriteBlobError> {
    use blobfs::blob::{
        Blob, NeedsData, NeedsTruncate, TruncateError, TruncateSuccess, WriteError, WriteSuccess,
    };

    fn truncate_result_to_status(res: &Result<TruncateSuccess, TruncateError>) -> Status {
        match res {
            Ok(_) => Status::OK,
            Err(TruncateError::NoSpace) => Status::NO_SPACE,
            Err(TruncateError::Corrupt) => Status::IO_DATA_INTEGRITY,
            Err(TruncateError::ConcurrentWrite)
            | Err(TruncateError::Fidl(_))
            | Err(TruncateError::UnexpectedResponse(_)) => Status::INTERNAL,
        }
    }

    fn write_result_to_status(res: &Result<WriteSuccess, WriteError>) -> Status {
        match res {
            Ok(_) => Status::OK,
            Err(WriteError::NoSpace) => Status::NO_SPACE,
            Err(WriteError::Corrupt) => Status::IO_DATA_INTEGRITY,
            Err(WriteError::Overwrite) => Status::IO,
            Err(WriteError::Fidl(_)) | Err(WriteError::UnexpectedResponse(_)) => Status::INTERNAL,
        }
    }

    let closer = blob.closer();

    enum State {
        ExpectTruncate(Blob<NeedsTruncate>),
        ExpectData(Blob<NeedsData>),
        ExpectClose,
    }

    impl State {
        fn expectation(&self) -> &'static str {
            match self {
                State::ExpectTruncate(_) => "resize",
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
                (fio::FileRequest::Resize { length, responder }, State::ExpectTruncate(blob)) => {
                    let res_fut = blob.truncate(length);
                    let guard = ftrace::async_enter!(
                        trace_id,
                        "app",
                        "waiting_for_blobfs_to_ack_truncate",
                        "size" => length
                    );
                    let res = res_fut.await;
                    let () = guard.end(&[]);

                    // Interpret responding errors as the stream closing unexpectedly.
                    let _: Result<(), fidl::Error> =
                        responder.send(&mut match truncate_result_to_status(&res) {
                            Status::OK => Ok(()),
                            error => Err(error.into_raw()),
                        });

                    // The empty blob needs no data and is complete after it is truncated.
                    match res? {
                        TruncateSuccess::Done(_) => State::ExpectClose,
                        TruncateSuccess::NeedsData(blob) => State::ExpectData(blob),
                    }
                }

                (fio::FileRequest::Write { data, responder }, State::ExpectData(blob)) => {
                    ftrace::async_instant!(
                        trace_id, "app", "read_chunk_from_pkg_resolver",
                        "size" => data.len() as u64
                    );

                    let res_fut = blob.write(&data);
                    let guard = ftrace::async_enter!(
                        trace_id,
                        "app",
                        "waiting_for_blobfs_to_ack_write",
                        "size" => data.len() as u64
                    );
                    let res = res_fut.await;
                    drop(guard);

                    let _: Result<(), fidl::Error> =
                        responder.send(&mut match write_result_to_status(&res) {
                            Status::OK => Ok(data.len() as u64),
                            error => Err(error.into_raw()),
                        });
                    ftrace::async_instant!(
                        trace_id, "app", "sent_write_ack_to_pkg_resolver",
                        "size" => data.len() as u64
                    );

                    match res? {
                        WriteSuccess::MoreToWrite(blob) => State::ExpectData(blob),
                        WriteSuccess::Done(_blob) => State::ExpectClose,
                    }
                }

                // Close is allowed in any state, but the blob is only written if we were expecting
                // a close.
                (fio::FileRequest::Close { responder }, state) => {
                    let () = close().await;
                    let _: Result<(), fidl::Error> = responder.send(&mut Ok(()));
                    return match state {
                        State::ExpectClose => Ok(()),
                        _ => Err(ServeWriteBlobError::UnexpectedClose),
                    };
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

/// Serves the `PackageIndexIteratorRequestStream` with as many base package index entries per
/// request as will fit in a fidl message.
async fn serve_base_package_index(
    package_host: &'static str,
    base_packages: Arc<BasePackages>,
    stream: PackageIndexIteratorRequestStream,
) {
    let mut package_entries = base_packages
        .paths_and_hashes()
        .map(|(path, hash)| PackageIndexEntry {
            package_url: fpkg::PackageUrl {
                url: format!("fuchsia-pkg://{}/{}", package_host, path.name()),
            },
            meta_far_blob_id: BlobId::from(hash.clone()).into(),
        })
        .collect::<Vec<PackageIndexEntry>>();
    package_entries.sort_unstable_by(|a, b| a.package_url.url.cmp(&b.package_url.url));
    serve_fidl_iterator_from_slice(stream, package_entries).await.unwrap_or_else(|e| {
        fx_log_err!("error serving PackageIndexIteratorRequestStream protocol: {:#}", anyhow!(e))
    })
}

/// Serves the `PackageIndexIteratorRequestStream` with as many cache package index entries per
/// request as will fit in a fidl message.
async fn serve_cache_package_index(
    cache_packages: Arc<Option<system_image::CachePackages>>,
    stream: PackageIndexIteratorRequestStream,
) {
    let package_entries = match &*cache_packages {
        Some(cache_packages) => cache_packages
            .contents()
            .map(|package_url| PackageIndexEntry {
                package_url: fpkg::PackageUrl {
                    url: package_url.as_unpinned().clone().clear_variant().to_string(),
                },
                meta_far_blob_id: BlobId::from(package_url.hash()).into(),
            })
            .collect::<Vec<PackageIndexEntry>>(),
        None => vec![],
    };
    serve_fidl_iterator_from_slice(stream, package_entries).await.unwrap_or_else(|e| {
        fx_log_err!("error serving PackageIndexIteratorRequestStream protocol: {:#}", anyhow!(e))
    })
}

#[cfg(test)]
mod serve_needed_blobs_tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_pkg::{BlobInfoIteratorMarker, BlobInfoIteratorProxy, NeededBlobsProxy},
        fuchsia_async::Timer,
        fuchsia_hash::HashRangeFull,
        fuchsia_inspect as finspect,
        futures::future::Either,
        std::time::Duration,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_stop() {
        let (_, stream) = fidl::endpoints::create_proxy_and_stream::<NeededBlobsMarker>().unwrap();

        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };

        let (blobfs, _) = blobfs::Client::new_test();
        let inspector = finspect::Inspector::new();
        let package_index = Arc::new(async_lock::RwLock::new(PackageIndex::new(
            inspector.root().create_child("test_does_not_use_inspect "),
        )));

        assert_matches!(
            serve_needed_blobs(
                stream,
                meta_blob_info,
                &package_index,
                &blobfs,
                crate::SubpackagesConfig::Disable,
                &inspector.root().create_child("test-node-name"),
                0.into()
            )
            .await,
            Err(ServeNeededBlobsError::UnexpectedClose("handle_open_meta_blob"))
        );
    }

    fn spawn_serve_needed_blobs_with_mocks(
        meta_blob_info: BlobInfo,
    ) -> (Task<Result<(), ServeNeededBlobsError>>, NeededBlobsProxy, blobfs::Mock) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<NeededBlobsMarker>().unwrap();

        let (blobfs, blobfs_mock) = blobfs::Client::new_mock();
        let inspector = finspect::Inspector::new();
        let package_index = Arc::new(async_lock::RwLock::new(PackageIndex::new(
            inspector.root().create_child("test_does_not_use_inspect "),
        )));

        (
            Task::spawn(async move {
                serve_needed_blobs(
                    stream,
                    meta_blob_info,
                    &package_index,
                    &blobfs,
                    crate::SubpackagesConfig::Disable,
                    &inspector.root().create_child("test-node-name"),
                    0.into(),
                )
                .await
                .map(|_| ())
            }),
            proxy,
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
        let (_, file_stream) = fidl::endpoints::create_request_stream::<fio::FileMarker>().unwrap();
        let (blobfs, _) = blobfs::Client::new_test();

        let mut response = FakeOpenBlobResponse::new();
        let res = open_write_blob(
            file_stream,
            response.responder(),
            &blobfs,
            [0; 32].into(),
            BlobKind::Package,
            0.into(),
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

        let (task, proxy, blobfs) = spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let (iter, iter_server_end) =
            fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
        proxy.get_missing_blobs(iter_server_end).unwrap();
        assert_matches!(
            iter.next().await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );

        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "get_missing_blobs",
                expected: "open_meta_blob"
            })
        );
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn expects_open_meta_blob_once() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 4 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        // Open a needed meta FAR blob and write it.
        let (serve_meta_task, ()) = future::join(
            // serve_meta_task does not complete until later.
            #[allow(clippy::async_yields_async)]
            async {
                blobfs.expect_create_blob([0; 32].into()).await.expect_payload(b"test").await;

                // serve_needed_blobs parses the meta far after it is written.  Feed that logic a
                // valid, minimal far that doesn't actually correlate to what we just wrote.
                serve_minimal_far(&mut blobfs, [0; 32].into()).await
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(true)));

                let () = blob
                    .resize(4)
                    .await
                    .expect("resize failed")
                    .map_err(Status::from_raw)
                    .expect("resize error");
                let _: u64 = blob
                    .write(b"test")
                    .await
                    .expect("write failed")
                    .map_err(Status::from_raw)
                    .expect("write error");
                let () = blob
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(Status::from_raw)
                    .expect("close error");
            },
        )
        .await;

        // Trying to open the meta FAR blob again after writing it successfully is a protocol violation.
        let (_blob, blob_server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
        assert_matches!(
            proxy.open_meta_blob(blob_server_end).await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );

        assert_matches!(
            serve_needed_task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "open_meta_blob",
                expected: "get_missing_blobs"
            })
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_present_meta_blob() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        // Try to open the meta FAR blob, but report it is no longer needed.
        let (serve_meta_task, ()) = future::join(
            // serve_meta_task does not complete until later.
            #[allow(clippy::async_yields_async)]
            async {
                blobfs.expect_create_blob([0; 32].into()).await.fail_open_with_already_exists();
                blobfs
                    .expect_open_blob([0; 32].into())
                    .await
                    .succeed_open_with_blob_readable()
                    .await;

                // serve_needed_blobs parses the meta far after it is written.  Feed that logic a
                // valid, minimal far that doesn't actually correlate to what we just wrote.
                serve_minimal_far(&mut blobfs, [0; 32].into()).await
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(false)));
                assert_matches!(
                    blob.resize(0).await,
                    Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
                );
            },
        )
        .await;

        // Trying to open the meta FAR blob again after being told it is not needed is a protocol
        // violation.
        let (_blob, blob_server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
        assert_matches!(
            proxy.open_meta_blob(blob_server_end).await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );

        assert_matches!(
            serve_needed_task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "open_meta_blob",
                expected: "get_missing_blobs"
            })
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn allows_retrying_nonfatal_open_meta_blob_errors() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 1 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        // Try to open the meta FAR blob, but report it is already being written concurrently.
        let ((), ()) = future::join(
            async {
                blobfs.expect_create_blob([0; 32].into()).await.fail_open_with_already_exists();
                blobfs.expect_open_blob([0; 32].into()).await.fail_open_with_not_readable().await;
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_meta_blob(blob_server_end).await,
                    Ok(Err(fidl_fuchsia_pkg::OpenBlobError::ConcurrentWrite))
                );
                assert_matches!(
                    blob.resize(1).await,
                    Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
                );
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
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(true)));

                let () = blob
                    .resize(1)
                    .await
                    .expect("resize failed")
                    .map_err(Status::from_raw)
                    .expect("resize error");
                let result =
                    blob.write(&mut [0]).await.expect("write failed").map_err(Status::from_raw);
                assert_eq!(result, Err(Status::IO_DATA_INTEGRITY));
                assert_matches!(
                    blob.close().await,
                    Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
                );
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
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(true)));

                let () = blob
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(Status::from_raw)
                    .expect("close error");
            },
        )
        .await;

        // Operation succeeds after blobfs cooperates.
        let (serve_meta_task, ()) = future::join(
            // serve_meta_task does not complete until later.
            #[allow(clippy::async_yields_async)]
            async {
                blobfs.expect_create_blob([0; 32].into()).await.expect_payload(&[0]).await;

                // serve_needed_blobs parses the meta far after it is written.  Feed that logic a
                // valid, minimal far that doesn't actually correlate to what we just wrote.
                serve_minimal_far(&mut blobfs, [0; 32].into()).await
            },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(true)));

                let () = blob
                    .resize(1)
                    .await
                    .expect("resize failed")
                    .map_err(Status::from_raw)
                    .expect("resize error");
                let _: u64 = blob
                    .write(&mut [0])
                    .await
                    .expect("write failed")
                    .map_err(Status::from_raw)
                    .expect("write error");
                let () = blob
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(Status::from_raw)
                    .expect("close error");
            },
        )
        .await;

        // Task moves to next state after retried write operation succeeds.
        let (_blob, blob_server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
        assert_matches!(
            proxy.open_meta_blob(blob_server_end).await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );
        assert_matches!(
            serve_needed_task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "open_meta_blob",
                expected: "get_missing_blobs"
            })
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    /// The returned task completes when the connection to the meta blob closes.
    pub(super) async fn serve_minimal_far(blobfs: &mut blobfs::Mock, meta_hash: Hash) -> Task<()> {
        let far_data = crate::test_utils::get_meta_far("fake-package", vec![]);

        let blob = blobfs.expect_open_blob(meta_hash.into()).await;
        Task::spawn(async move { blob.serve_contents(&far_data[..]).await })
    }

    /// The returned task completes when the connection to the meta blob closes, which is normally
    /// when the task serving the NeededBlobs stream completes.
    pub(super) async fn write_meta_blob(
        proxy: &NeededBlobsProxy,
        blobfs: &mut blobfs::Mock,
        meta_blob_info: BlobInfo,
        needed_blobs: impl IntoIterator<Item = Hash>,
    ) -> Task<()> {
        let far_data = crate::test_utils::get_meta_far("fake-package", needed_blobs);

        let (serve_contents, ()) = future::join(
            // serve_contents does not complete until later.
            #[allow(clippy::async_yields_async)]
            async {
                // Fail the create request, then succeed an open request that checks if the blob is
                // readable. The already_exists error could indicate that the blob is being
                // written, so pkg-cache needs to disambiguate the 2 cases.
                blobfs
                    .expect_create_blob(meta_blob_info.blob_id.into())
                    .await
                    .fail_open_with_already_exists();
                blobfs
                    .expect_open_blob(meta_blob_info.blob_id.into())
                    .await
                    .succeed_open_with_blob_readable()
                    .await;

                let blob = blobfs.expect_open_blob(meta_blob_info.blob_id.into()).await;

                // the serving task does not complete until later.
                #[allow(clippy::async_yields_async)]
                Task::spawn(async move { blob.serve_contents(&far_data[..]).await })
            },
            async {
                let (_blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(false)));
            },
        )
        .await;
        serve_contents
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
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let expected = HashRangeFull::default().skip(1).take(2000).collect::<Vec<_>>();

        let serve_meta_task =
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
            serve_needed_task.await,
            Err(ServeNeededBlobsError::UnexpectedClose("handle_open_blobs"))
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_no_missing_blobs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let serve_meta_task = write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![]).await;

        let (missing_blobs_iter, missing_blobs_iter_server_end) =
            fidl::endpoints::create_proxy::<BlobInfoIteratorMarker>().unwrap();
        assert_matches!(proxy.get_missing_blobs(missing_blobs_iter_server_end), Ok(()));
        let missing_blobs = collect_blob_info_iterator(missing_blobs_iter).await;
        assert_eq!(missing_blobs, vec![]);

        assert_matches!(serve_needed_task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }))
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fails_on_invalid_meta_far() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, mut blobfs) = spawn_serve_needed_blobs_with_mocks(meta_blob_info);

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
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(proxy.open_meta_blob(blob_server_end).await, Ok(Ok(false)));
            },
        )
        .await;

        drop(proxy);
        assert_matches!(
            task.await,
            Err(ServeNeededBlobsError::FulfillMetaFar(FulfillMetaFarError::CreateRootDir(
                package_directory::Error::ArchiveReader(fuchsia_archive::Error::InvalidMagic(_))
            )))
        );
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn dropping_needed_blobs_stops_missing_blob_iterator() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let missing = HashRangeFull::default().take(10).collect::<Vec<_>>();
        let serve_meta_task =
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
            serve_needed_task.await,
            Err(ServeNeededBlobsError::UnexpectedClose("handle_open_blobs"))
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn expects_get_missing_blobs_once() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let missing = HashRangeFull::default().take(10).collect::<Vec<_>>();
        let serve_meta_task =
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
            serve_needed_task.await,
            Err(ServeNeededBlobsError::UnexpectedRequest {
                received: "get_missing_blobs",
                expected: "open_blob"
            })
        );
        let () = serve_meta_task.await;
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
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let serve_meta_task =
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
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from([2; 32]).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let () = blob
                    .resize(payload.len() as u64)
                    .await
                    .expect("resize failed")
                    .map_err(Status::from_raw)
                    .expect("resize error");
                let _: u64 = blob
                    .write(payload)
                    .await
                    .expect("write failed")
                    .map_err(Status::from_raw)
                    .expect("write error");
                let () = blob
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(Status::from_raw)
                    .expect("close error");
            },
        )
        .await;

        assert_matches!(serve_needed_task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }))
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_many_content_blobs_that_need_written() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let content_blobs = || HashRangeFull::default().skip(1).take(100);

        let serve_meta_task =
            write_meta_blob(&proxy, &mut blobfs, meta_blob_info, content_blobs()).await;
        enumerate_readable_missing_blobs(&proxy, &mut blobfs, std::iter::empty(), content_blobs())
            .await;

        fn payload(hash: Hash) -> Vec<u8> {
            let hash_bytes = || hash.as_bytes().iter().copied();
            let len = hash_bytes().map(|n| n as usize).sum();
            assert!(len <= fio::MAX_BUF as usize);

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
                            fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                        let open_fut =
                            proxy.open_blob(&mut BlobId::from(hash).into(), blob_server_end);

                        async move {
                            assert_matches!(open_fut.await, Ok(Ok(true)));

                            let payload = payload(hash);
                            let () = blob
                                .resize(payload.len() as u64)
                                .await
                                .expect("resize failed")
                                .map_err(Status::from_raw)
                                .expect("resize error");
                            let _: u64 = blob
                                .write(&payload)
                                .await
                                .expect("write failed")
                                .map_err(Status::from_raw)
                                .expect("write error");
                            let () = blob
                                .close()
                                .await
                                .expect("close failed")
                                .map_err(Status::from_raw)
                                .expect("close error");
                        }
                    })
                    .await;
            },
        )
        .await;

        assert_matches!(serve_needed_task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }))
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_many_content_blobs_that_are_already_present() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let content_blobs = || HashRangeFull::default().skip(1).take(100);

        let serve_meta_task =
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
                            fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

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

        assert_matches!(serve_needed_task.await, Ok(()));
        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. }))
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn allows_retrying_nonfatal_open_blob_errors() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let content_blob = Hash::from([1; 32]);

        let serve_meta_task =
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
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end).await,
                    Ok(Err(fidl_fuchsia_pkg::OpenBlobError::ConcurrentWrite))
                );
                assert_matches!(
                    blob.resize(1).await,
                    Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
                );
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
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let () = blob
                    .resize(1)
                    .await
                    .expect("resize failed")
                    .map_err(Status::from_raw)
                    .expect("resize error");
                let result =
                    blob.write(&mut [0]).await.expect("write failed").map_err(Status::from_raw);
                assert_eq!(result, Err(Status::IO_DATA_INTEGRITY));
                assert_matches!(
                    blob.close().await,
                    Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
                );
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
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let () = blob
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(Status::from_raw)
                    .expect("close error");
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
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

                assert_matches!(
                    proxy.open_blob(&mut BlobId::from(content_blob).into(), blob_server_end).await,
                    Ok(Ok(true))
                );

                let () = blob
                    .resize(1)
                    .await
                    .expect("resize failed")
                    .map_err(Status::from_raw)
                    .expect("resize error");
                let _: u64 = blob
                    .write(&mut [0])
                    .await
                    .expect("write failed")
                    .map_err(Status::from_raw)
                    .expect("write error");
                let () = blob
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(Status::from_raw)
                    .expect("close error");
            },
        )
        .await;

        // That was the only data blob, so the operation is now done.
        assert_matches!(serve_needed_task.await, Ok(()));
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn abort_aborts_while_waiting_for_open_meta_blob() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (task, proxy, blobfs) = spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let abort_fut = proxy.abort();

        assert_matches!(task.await, Err(ServeNeededBlobsError::Aborted));
        assert_matches!(
            abort_fut.await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn abort_waits_for_pending_blob_writes_before_responding() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let content_blob = Hash::from([1; 32]);

        let serve_meta_task =
            write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![content_blob]).await;
        enumerate_readable_missing_blobs(
            &proxy,
            &mut blobfs,
            std::iter::empty(),
            vec![content_blob].into_iter(),
        )
        .await;

        let payload = b"pending blob write";

        #[allow(clippy::async_yields_async)]
        // We want to join these futures, it's okay to not await them.
        let (pending_blob_mock_fut, blob) = future::join(
            async { blobfs.expect_create_blob(content_blob).await.expect_payload(payload) },
            async {
                let (blob, blob_server_end) =
                    fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();

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
                let () = blob
                    .resize(payload.len() as u64)
                    .await
                    .expect("resize failed")
                    .map_err(Status::from_raw)
                    .expect("resize error");
                let _: u64 = blob
                    .write(payload)
                    .await
                    .expect("write failed")
                    .map_err(Status::from_raw)
                    .expect("write error");
                let () = blob
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(Status::from_raw)
                    .expect("close error");
            },
        )
        .await;

        assert_matches!(
            abort_fut.await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );

        assert_matches!(serve_needed_task.await, Err(ServeNeededBlobsError::Aborted));
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn abort_aborts_while_waiting_for_get_missing_blobs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let serve_meta_task = write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![]).await;

        let abort_fut = proxy.abort();

        assert_matches!(serve_needed_task.await, Err(ServeNeededBlobsError::Aborted));
        assert_matches!(
            abort_fut.await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn abort_aborts_while_waiting_for_open_blobs() {
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (serve_needed_task, proxy, mut blobfs) =
            spawn_serve_needed_blobs_with_mocks(meta_blob_info);

        let serve_meta_task =
            write_meta_blob(&proxy, &mut blobfs, meta_blob_info, vec![[2; 32].into()]).await;
        enumerate_readable_missing_blobs(
            &proxy,
            &mut blobfs,
            std::iter::empty(),
            vec![[2; 32].into()].into_iter(),
        )
        .await;

        let abort_fut = proxy.abort();

        assert_matches!(serve_needed_task.await, Err(ServeNeededBlobsError::Aborted));
        assert_matches!(
            abort_fut.await,
            Err(fidl::Error::ClientChannelClosed { status: Status::PEER_CLOSED, .. })
        );
        let () = serve_meta_task.await;
        blobfs.expect_done().await;
    }
}

#[cfg(test)]
mod get_handler_tests {
    use {
        super::*,
        crate::{CobaltConnectedService, ProtocolConnector, COBALT_CONNECTOR_BUFFER_SIZE},
        std::collections::HashSet,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn everything_closed() {
        let (_, stream) = fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
        let meta_blob_info = BlobInfo { blob_id: [0; 32].into(), length: 0 };
        let (blobfs, _) = blobfs::Client::new_test();
        let inspector = fuchsia_inspect::Inspector::new();
        let package_index = Arc::new(async_lock::RwLock::new(PackageIndex::new(
            inspector.root().create_child("test_does_not_use_inspect "),
        )));

        assert_matches::assert_matches!(
            get(
                &package_index,
                &BasePackages::new_test_only(HashSet::new(), vec![]),
                system_image::ExecutabilityRestrictions::DoNotEnforce,
                &system_image::NonStaticAllowList::empty(),
                &blobfs,
                meta_blob_info,
                stream,
                None,
                crate::SubpackagesConfig::Disable,
                ProtocolConnector::new_with_buffer_size(
                    CobaltConnectedService,
                    COBALT_CONNECTOR_BUFFER_SIZE,
                )
                .serve_and_log_errors()
                .0,
                &inspector.root().create_child("get"),
                0.into()
            )
            .await,
            Err(Status::UNAVAILABLE)
        );
    }
}

#[cfg(test)]
mod serve_write_blob_tests {
    use {
        super::*, assert_matches::assert_matches, futures::task::Poll, proptest::prelude::*,
        proptest_derive::Arbitrary,
    };

    /// Calls the provided test function with an open File proxy being served by serve_write_blob
    /// and the corresponding request stream representing the open blobfs file.
    async fn do_serve_write_blob_with<F, Fut>(cb: F) -> Result<(), ServeWriteBlobError>
    where
        F: FnOnce(fio::FileProxy, fio::FileRequestStream) -> Fut,
        Fut: Future<Output = ()>,
    {
        let (blobfs_blob, blobfs_blob_stream) = blobfs::blob::Blob::new_test();

        let (pkg_cache_blob, pkg_cache_blob_stream) =
            fidl::endpoints::create_proxy_and_stream::<fio::FileMarker>().unwrap();

        let task = serve_write_blob(pkg_cache_blob_stream, blobfs_blob, 0.into());
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
        proxy: &fio::FileProxy,
        stream: &mut fio::FileRequestStream,
        length: u64,
        blobfs_response: Status,
    ) -> Status {
        let ((), status) = future::join(
            async move {
                serve_fidl_request!(stream, {
                    fio::FileRequest::Resize { length: actual_length, responder } => {
                        assert_eq!(length, actual_length);
                            let () = responder.send(&mut if blobfs_response == Status::OK {
                            Ok(())
                        } else {
                        Err(blobfs_response.into_raw())
                            }).unwrap();
                    },
                });

                // Also expect the client to close the blob on truncate error.
                if blobfs_response != Status::OK {
                    serve_fidl_request!(stream, {
                        fio::FileRequest::Close { responder } => {
                            responder.send(&mut Ok(())).unwrap();
                        },
                    });
                }
            },
            proxy.resize(length).map(|result| match result.unwrap().map_err(Status::from_raw) {
                Ok(()) => Status::OK,
                Err(status) => status,
            }),
        )
        .await;
        status
    }

    /// Sends a write request, asserts that the remote end receives the request, responds to the
    /// request, and asserts that the write request receives the expected mapped status code/length.
    async fn verify_write(
        proxy: &fio::FileProxy,
        stream: &mut fio::FileRequestStream,
        data: &[u8],
        blobfs_response: Status,
    ) -> Status {
        let ((), o) = future::join(
            async move {
                serve_fidl_request!(stream, {
                    fio::FileRequest::Write { data: actual_data, responder } => {
                        assert_eq!(data, actual_data);
                        let () = responder.send(&mut if blobfs_response == zx::Status::OK {
                            Ok(data.len() as u64)
                        } else {
                            Err(blobfs_response.into_raw())
                        }).unwrap();
                    },
                });

                // Also expect the client to close the blob on write error.
                if blobfs_response != Status::OK {
                    serve_fidl_request!(stream, {
                        fio::FileRequest::Close { responder } => {
                            responder.send(&mut Ok(())).unwrap();
                        },
                    });
                }
            },
            async move {
                match proxy.write(data).await.unwrap().map_err(Status::from_raw) {
                    Ok(len) => {
                        assert_eq!(len, data.len() as u64);
                        Status::OK
                    }
                    Err(status) => status,
                }
            },
        )
        .await;
        o
    }

    /// Verify that closing the proxy results in the blobfs backing file being explicitly closed.
    async fn verify_inner_blob_closes(proxy: fio::FileProxy, mut stream: fio::FileRequestStream) {
        drop(proxy);
        serve_fidl_stream!(stream, {
            fio::FileRequest::Close { responder } => {
                responder.send(&mut Ok(())).unwrap();
            },
        })
        .await;
    }

    /// Verify that an explicit close() request is proxied through to the blobfs backing file.
    async fn verify_explicit_close(proxy: fio::FileProxy, mut stream: fio::FileRequestStream) {
        let ((), ()) = future::join(
            serve_fidl_stream!(stream, {
                fio::FileRequest::Close { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                },
            }),
            async move {
                let () = proxy
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(Status::from_raw)
                    .expect("close error");
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
            fidl::endpoints::create_proxy_and_stream::<fio::FileMarker>().unwrap();

        let task = serve_write_blob(pkg_cache_blob_stream, blobfs_blob, 0.into());
        futures::pin_mut!(task);

        let mut close_fut = pkg_cache_blob.close();
        drop(pkg_cache_blob);

        // Let the task process the close request, ensuring the close_future doesn't yet complete.
        assert_matches!(executor.run_until_stalled(&mut task), Poll::Pending);
        assert_matches!(executor.run_until_stalled(&mut close_fut), Poll::Pending);

        // Verify the inner blob is bineg closed.
        let () = executor.run_singlethreaded(async {
            serve_fidl_request!(blobfs_blob_stream, {
                fio::FileRequest::Close { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                },
            })
        });

        // Now that the inner blob is closed, the proxy task and close request can complete
        assert_matches!(
            executor.run_until_stalled(&mut task),
            Poll::Ready(Err(ServeWriteBlobError::UnexpectedClose))
        );
        assert_matches!(executor.run_until_stalled(&mut close_fut), Poll::Ready(Ok(Ok(()))));
    }

    #[derive(Debug, Clone, Copy, PartialEq, Eq, Arbitrary)]
    enum StubRequestor {
        Clone,
        DescribeDeprecated,
        Sync,
        GetAttr,
        SetAttr,
        Write,
        WriteAt,
        Read,
        ReadAt,
        Seek,
        Truncate,
        GetFlags,
        SetFlags,
        GetBackingMemory,
        // New API. Not strictly necessary to verify all possible ordinals (which is the space of a
        // u64 anyway).
        // AdvisoryLock

        // Always allowed.
        // Close
    }

    impl StubRequestor {
        fn method_name(self) -> &'static str {
            match self {
                StubRequestor::Clone => "clone",
                StubRequestor::DescribeDeprecated => "describe_deprecated",
                StubRequestor::Sync => "sync",
                StubRequestor::GetAttr => "get_attr",
                StubRequestor::SetAttr => "set_attr",
                StubRequestor::Write => "write",
                StubRequestor::WriteAt => "write_at",
                StubRequestor::Read => "read",
                StubRequestor::ReadAt => "read_at",
                StubRequestor::Seek => "seek",
                StubRequestor::Truncate => "resize",
                StubRequestor::GetFlags => "get_flags",
                StubRequestor::SetFlags => "set_flags",
                StubRequestor::GetBackingMemory => "get_backing_memory",
            }
        }

        fn make_stub_request(self, proxy: &fio::FileProxy) -> impl Future<Output = ()> {
            use fidl::encoding::Decodable;
            match self {
                StubRequestor::Clone => {
                    let (_, server_end) =
                        fidl::endpoints::create_proxy::<fio::NodeMarker>().unwrap();
                    let () = proxy.clone(fio::OpenFlags::empty(), server_end).unwrap();
                    future::ready(()).boxed()
                }
                StubRequestor::DescribeDeprecated => {
                    proxy.describe_deprecated().map(|_| ()).boxed()
                }
                StubRequestor::Sync => proxy.sync().map(|_| ()).boxed(),
                StubRequestor::GetAttr => proxy.get_attr().map(|_| ()).boxed(),
                StubRequestor::SetAttr => proxy
                    .set_attr(
                        fio::NodeAttributeFlags::empty(),
                        &mut fio::NodeAttributes::new_empty(),
                    )
                    .map(|_| ())
                    .boxed(),
                StubRequestor::Write => proxy.write(&[0; 0]).map(|_| ()).boxed(),
                StubRequestor::WriteAt => proxy.write_at(&[0; 0], 0).map(|_| ()).boxed(),
                StubRequestor::Read => proxy.read(0).map(|_| ()).boxed(),
                StubRequestor::ReadAt => proxy.read_at(0, 0).map(|_| ()).boxed(),
                StubRequestor::Seek => proxy.seek(fio::SeekOrigin::Start, 0).map(|_| ()).boxed(),
                StubRequestor::Truncate => proxy.resize(0).map(|_| ()).boxed(),
                StubRequestor::GetFlags => proxy.get_flags().map(|_| ()).boxed(),
                StubRequestor::SetFlags => {
                    proxy.set_flags(fio::OpenFlags::empty()).map(|_| ()).boxed()
                }
                StubRequestor::GetBackingMemory => {
                    proxy.get_backing_memory(fio::VmoFlags::empty()).map(|_| ()).boxed()
                }
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
                InitialState::ExpectTruncate => "resize",
                InitialState::ExpectWrite => "write",
                InitialState::ExpectClose => "close",
            }
        }

        async fn enter(self, proxy: &fio::FileProxy, stream: &mut fio::FileRequestStream) {
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
                            let () = bad_request_fut.await;
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
    use {
        super::*, fidl_fuchsia_pkg::PackageIndexIteratorMarker, fuchsia_pkg::PackagePath,
        std::collections::HashSet,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn base_packages_entries_converted_correctly() {
        let base_packages = BasePackages::new_test_only(
            HashSet::new(),
            [
                (
                    PackagePath::from_name_and_variant(
                        "name0".parse().unwrap(),
                        "0".parse().unwrap(),
                    ),
                    Hash::from([0u8; 32]),
                ),
                (
                    PackagePath::from_name_and_variant(
                        "name1".parse().unwrap(),
                        "1".parse().unwrap(),
                    ),
                    Hash::from([1u8; 32]),
                ),
            ],
        );

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PackageIndexIteratorMarker>().unwrap();
        let task =
            Task::local(serve_base_package_index("fuchsia.test", Arc::new(base_packages), stream));

        let entries = proxy.next().await.unwrap();
        assert_eq!(
            entries,
            vec![
                fidl_fuchsia_pkg::PackageIndexEntry {
                    package_url: fpkg::PackageUrl {
                        url: "fuchsia-pkg://fuchsia.test/name0".to_string(),
                    },
                    meta_far_blob_id: fidl_fuchsia_pkg::BlobId { merkle_root: [0u8; 32] }
                },
                fidl_fuchsia_pkg::PackageIndexEntry {
                    package_url: fpkg::PackageUrl {
                        url: "fuchsia-pkg://fuchsia.test/name1".to_string(),
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

#[cfg(test)]
mod serve_cache_package_index_tests {
    use {
        super::*, fidl_fuchsia_pkg::PackageIndexIteratorMarker,
        fuchsia_url::PinnedAbsolutePackageUrl,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cache_packages_entries_converted_correctly() {
        let cache_packages = system_image::CachePackages::from_entries(vec![
            PinnedAbsolutePackageUrl::new(
                "fuchsia-pkg://fuchsia.test".parse().unwrap(),
                "name0".parse().unwrap(),
                Some(fuchsia_url::PackageVariant::zero()),
                Hash::from([0u8; 32]),
            ),
            PinnedAbsolutePackageUrl::new(
                "fuchsia-pkg://fuchsia.test".parse().unwrap(),
                "name1".parse().unwrap(),
                Some("1".parse().unwrap()),
                Hash::from([1u8; 32]),
            ),
        ]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PackageIndexIteratorMarker>().unwrap();
        let task = Task::local(serve_cache_package_index(Arc::new(Some(cache_packages)), stream));

        let entries = proxy.next().await.unwrap();
        assert_eq!(
            entries,
            vec![
                fidl_fuchsia_pkg::PackageIndexEntry {
                    package_url: fpkg::PackageUrl {
                        url: "fuchsia-pkg://fuchsia.test/name0".to_string()
                    },
                    meta_far_blob_id: fidl_fuchsia_pkg::BlobId { merkle_root: [0u8; 32] }
                },
                fidl_fuchsia_pkg::PackageIndexEntry {
                    package_url: fpkg::PackageUrl {
                        url: "fuchsia-pkg://fuchsia.test/name1".to_string()
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
