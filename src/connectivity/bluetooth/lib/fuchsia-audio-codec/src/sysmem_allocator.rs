// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::{client::QueryResponseFut, endpoints::ClientEnd},
    fidl_fuchsia_sysmem::{
        AllocatorProxy, BufferCollectionConstraints, BufferCollectionInfo2, BufferCollectionMarker,
        BufferCollectionProxy, BufferCollectionTokenMarker, BufferMemorySettings,
    },
    fuchsia_zircon as zx,
    futures::{
        future::{FusedFuture, Future},
        ready,
        task::{Context, Poll},
        FutureExt,
    },
    log::error,
    std::convert::TryInto,
    std::pin::Pin,
};

/// A set of buffers that have been allocated with the SysmemAllocator.
#[derive(Debug)]
pub struct SysmemAllocatedBuffers {
    buffers: Vec<zx::Vmo>,
    settings: BufferMemorySettings,
    _buffer_collection: BufferCollectionProxy,
}

impl SysmemAllocatedBuffers {
    /// Settings of the buffers that are available through `SysmemAllocator::get`
    /// Returns None if the buffers are not allocated yet.
    pub fn settings(&self) -> &BufferMemorySettings {
        &self.settings
    }

    /// Get a VMO which has been allocated from the
    pub fn get_mut(&mut self, idx: u32) -> Option<&mut zx::Vmo> {
        let idx = idx as usize;
        return self.buffers.get_mut(idx);
    }

    /// Get the number of VMOs that have been allocated.
    /// Returns None if the allocation is not complete yet.
    pub fn len(&self) -> u32 {
        self.buffers.len().try_into().expect("buffers should fit in u32")
    }
}

/// A Future that communicates with the `fuchsia.sysmem.Allocator` service to allocate shared
/// buffers.
pub enum SysmemAllocation {
    Pending,
    /// Waiting for the Sync response from the Allocator
    WaitingForSync {
        future: QueryResponseFut<()>,
        token_fn: Option<Box<dyn FnOnce() -> () + Send + Sync>>,
        buffer_collection: BufferCollectionProxy,
    },
    /// Waiting for the buffers to be allocated, which should eventually happen after delivering the token.
    WaitingForAllocation(
        QueryResponseFut<(zx::zx_status_t, BufferCollectionInfo2)>,
        BufferCollectionProxy,
    ),
    /// Allocation is completed. The status here represents whether it completed successfully (ZX_OK) or an error.
    Done(zx::Status),
}

impl SysmemAllocation {
    /// A pending allocation which has not been started, and will never finish.
    pub fn pending() -> Self {
        Self::Pending
    }

    /// Allocate a new shared memory collection, using `allocator` to communicate with the Allocator
    /// service. `constraints` will be used to allocate the collection. A shared collection token
    /// client end will be provided to the `token_target_fn` once the request has been synced with
    /// the collection. This token can be used with `SysmemAllocation::shared` to finish allocating
    /// the shared buffers or provided to another service to share allocation, or duplicated to
    /// share this memory with more than one other client.
    pub fn allocate<
        F: FnOnce(ClientEnd<BufferCollectionTokenMarker>) -> () + 'static + Send + Sync,
    >(
        allocator: AllocatorProxy,
        constraints: BufferCollectionConstraints,
        token_target_fn: F,
    ) -> Result<Self, Error> {
        let (client_token, client_token_request) =
            fidl::endpoints::create_proxy::<BufferCollectionTokenMarker>()?;
        allocator
            .allocate_shared_collection(client_token_request)
            .context("Allocating shared collection")?;

        // Duplicate to get another BufferCollectionToken to the same collection.
        let (token, token_request) = fidl::endpoints::create_endpoints()?;
        client_token.duplicate(std::u32::MAX, token_request)?;

        let client_end_token =
            ClientEnd::new(client_token.into_channel().unwrap().into_zx_channel());

        let mut res = Self::bind(allocator, client_end_token, constraints)?;

        if let Self::WaitingForSync { token_fn, .. } = &mut res {
            token_fn.replace(Box::new(move || token_target_fn(token)));
        }

        Ok(res)
    }

    /// Bind to a shared memory collection, using `allocator` to communicate with the Allocator
    /// service and a `token` which has already been allocated. `constraints` is set to communicate
    /// the requirements of this client.
    pub fn bind(
        allocator: AllocatorProxy,
        token: ClientEnd<BufferCollectionTokenMarker>,
        mut constraints: BufferCollectionConstraints,
    ) -> Result<Self, Error> {
        let (buffer_collection, collection_request) =
            fidl::endpoints::create_proxy::<BufferCollectionMarker>()?;
        allocator.bind_shared_collection(token, collection_request)?;

        buffer_collection
            .set_constraints(true, &mut constraints)
            .context("sending constraints to sysmem")?;

        Ok(Self::WaitingForSync {
            future: buffer_collection.sync(),
            token_fn: None,
            buffer_collection,
        })
    }

    /// Advances a synced collection to wait for the allocation of the buffers, after synced.
    /// Delivers the token to the target as the collection is aware of it now and can reliably
    /// detect when all tokens have been turned in and constraints have been set.
    fn synced(&mut self) -> Result<(), Error> {
        *self = match std::mem::replace(self, Self::Done(zx::Status::BAD_STATE)) {
            Self::WaitingForSync { future: _, token_fn, buffer_collection } => {
                if let Some(deliver_token_fn) = token_fn {
                    deliver_token_fn();
                }
                Self::WaitingForAllocation(
                    buffer_collection.wait_for_buffers_allocated(),
                    buffer_collection,
                )
            }
            _ => Self::Done(zx::Status::BAD_STATE),
        };
        if let Self::Done(e) = self {
            return Err(e.into_io_error().into());
        }
        Ok(())
    }

    /// Finish once the allocation has completed.  Returns the buffers and marks the allocation as
    /// complete.
    fn allocated(
        &mut self,
        status: zx::zx_status_t,
        mut buffer_info: BufferCollectionInfo2,
    ) -> Result<SysmemAllocatedBuffers, Error> {
        match std::mem::replace(self, Self::Done(zx::Status::from_raw(status))) {
            Self::WaitingForAllocation(_, buffer_collection) => {
                let num_buffers = buffer_info.buffer_count.try_into()?;
                let mut buffers = Vec::new();
                for buffer in buffer_info.buffers[0..num_buffers].iter_mut() {
                    buffers.push(buffer.vmo.take().ok_or(format_err!("missing buffer"))?);
                }

                Ok(SysmemAllocatedBuffers {
                    buffers,
                    settings: buffer_info.settings.buffer_settings,
                    _buffer_collection: buffer_collection,
                })
            }
            _ => Err(format_err!("allocation complete but not in the right state")),
        }
    }
}

impl FusedFuture for SysmemAllocation {
    fn is_terminated(&self) -> bool {
        match self {
            Self::Done(_) => true,
            _ => false,
        }
    }
}

impl Future for SysmemAllocation {
    type Output = Result<SysmemAllocatedBuffers, Error>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let s = Pin::into_inner(self);
        if let Self::WaitingForSync { future, .. } = s {
            match ready!(future.poll_unpin(cx)) {
                Err(e) => {
                    error!("SysmemAllocator error: {:?}", e);
                    return Poll::Ready(Err(e.into()));
                }
                Ok(()) => {
                    if let Err(e) = s.synced() {
                        return Poll::Ready(Err(e));
                    }
                }
            };
        }
        if let Self::WaitingForAllocation(future, _) = s {
            match ready!(future.poll_unpin(cx)) {
                Ok((status, buffer_collection)) => {
                    return Poll::Ready(s.allocated(status, buffer_collection))
                }
                Err(e) => {
                    error!("SysmemAllocator waiting error: {:?}", e);
                    Poll::Ready(Err(e.into()))
                }
            }
        } else {
            Poll::Pending
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::encoding::Decodable;
    use fidl_fuchsia_sysmem::{
        AllocatorMarker, AllocatorRequest, BufferCollectionRequest, BufferCollectionTokenProxy,
        BufferCollectionTokenRequest, BufferCollectionTokenRequestStream, CoherencyDomain,
        HeapType, ImageFormatConstraints, SingleBufferSettings, VmoBuffer,
    };
    use fuchsia_async::{self as fasync, pin_mut};
    use futures::StreamExt;

    use crate::buffer_collection_constraints::BUFFER_COLLECTION_CONSTRAINTS_DEFAULT;

    fn assert_tokens_connected(
        exec: &mut fasync::Executor,
        proxy: &BufferCollectionTokenProxy,
        requests: &mut BufferCollectionTokenRequestStream,
    ) {
        let mut sync_fut = proxy.sync();

        match exec.run_until_stalled(&mut requests.next()) {
            Poll::Ready(Some(Ok(BufferCollectionTokenRequest::Sync { responder }))) => {
                responder.send().expect("respond to sync")
            }
            x => panic!("Expected vended token to be connected, got {:?}", x),
        };

        // The sync future is ready now.
        assert!(exec.run_until_stalled(&mut sync_fut).is_ready());
    }

    #[test]
    fn allocate_future() {
        let mut exec = fasync::Executor::new().expect("executor creation");

        let (proxy, mut allocator_requests) =
            fidl::endpoints::create_proxy_and_stream::<AllocatorMarker>().unwrap();

        let (sender, mut receiver) = futures::channel::oneshot::channel();

        let token_fn = move |token| {
            sender.send(token).expect("should be able to send token");
        };

        let mut allocation =
            SysmemAllocation::allocate(proxy, BUFFER_COLLECTION_CONSTRAINTS_DEFAULT, token_fn)
                .expect("starting should work");

        let mut token_requests_1 = match exec.run_until_stalled(&mut allocator_requests.next()) {
            Poll::Ready(Some(Ok(AllocatorRequest::AllocateSharedCollection {
                token_request,
                ..
            }))) => token_request.into_stream().expect("request into stream"),
            x => panic!("Expected a shared allocation request, got {:?}", x),
        };

        let mut token_requests_2 = match exec.run_until_stalled(&mut token_requests_1.next()) {
            Poll::Ready(Some(Ok(BufferCollectionTokenRequest::Duplicate {
                rights_attenuation_mask: _,
                token_request,
                ..
            }))) => token_request.into_stream().expect("duplicate request into stream"),
            x => panic!("Expected a duplication request, got {:?}", x),
        };

        let (token_client_1, mut collection_requests_1) = match exec
            .run_until_stalled(&mut allocator_requests.next())
        {
            Poll::Ready(Some(Ok(AllocatorRequest::BindSharedCollection {
                token,
                buffer_collection_request,
                ..
            }))) => (
                token.into_proxy().unwrap(),
                buffer_collection_request.into_stream().expect("collection request into stream"),
            ),
            x => panic!("Expected Bind Shared Collection, got: {:?}", x),
        };

        // The token turned into the allocator for binding should be connected to the server on allocating.
        assert_tokens_connected(&mut exec, &token_client_1, &mut token_requests_1);

        match exec.run_until_stalled(&mut collection_requests_1.next()) {
            Poll::Ready(Some(Ok(BufferCollectionRequest::SetConstraints { .. }))) => {}
            x => panic!("Expected buffer constraints request, got {:?}", x),
        };

        let sync_responder = match exec.run_until_stalled(&mut collection_requests_1.next()) {
            Poll::Ready(Some(Ok(BufferCollectionRequest::Sync { responder }))) => responder,
            x => panic!("Expected a sync request, got {:?}", x),
        };

        // The sysmem allocator is now waiting for the sync from the collection

        assert!(exec.run_until_stalled(&mut allocation).is_pending());

        // When it gets a response that the collection is synced, it vends the token out
        sync_responder.send().expect("respond to sync request");

        assert!(exec.run_until_stalled(&mut allocation).is_pending());

        let token_client_2 = match receiver.try_recv() {
            Ok(Some(token)) => token.into_proxy().unwrap(),
            x => panic!("Should have a token sent to the fn, got {:?}", x),
        };

        // token_client_2 should be attached to the token_requests_2 that we handed over to sysmem
        // (in the token duplicate)
        assert_tokens_connected(&mut exec, &token_client_2, &mut token_requests_2);

        // We should have received a wait for the buffers to be allocated in our collection
        const SIZE_BYTES: u32 = 1024;
        let buffer_settings = BufferMemorySettings {
            size_bytes: SIZE_BYTES,
            is_physically_contiguous: true,
            is_secure: false,
            coherency_domain: CoherencyDomain::Ram,
            heap: HeapType::SystemRam,
        };

        match exec.run_until_stalled(&mut collection_requests_1.next()) {
            Poll::Ready(Some(Ok(BufferCollectionRequest::WaitForBuffersAllocated {
                responder,
            }))) => {
                let single_buffer_settings = SingleBufferSettings {
                    buffer_settings,
                    has_image_format_constraints: false,
                    image_format_constraints: ImageFormatConstraints::new_empty(),
                };
                let mut buffer_collection_info = BufferCollectionInfo2 {
                    buffer_count: 1,
                    settings: single_buffer_settings,
                    ..BufferCollectionInfo2::new_empty()
                };

                buffer_collection_info.buffers[0] = VmoBuffer {
                    vmo: Some(zx::Vmo::create(SIZE_BYTES.into()).expect("vmo creation")),
                    vmo_usable_start: 0,
                };
                responder
                    .send(zx::Status::OK.into_raw(), &mut buffer_collection_info)
                    .expect("send collection response")
            }
            x => panic!("Expected WaitForBuffersAllocated, got {:?}", x),
        };

        // The allocator should now be finished!
        let mut buffers = match exec.run_until_stalled(&mut allocation) {
            Poll::Pending => panic!("allocation should be done"),
            Poll::Ready(res) => res.expect("successful allocation"),
        };

        assert_eq!(1, buffers.len());
        assert!(buffers.get_mut(0).is_some());
        assert_eq!(buffers.settings(), &buffer_settings);
    }

    #[test]
    fn with_system_allocator() {
        let mut exec = fasync::Executor::new().expect("executor creation");
        let sysmem_client = fuchsia_component::client::connect_to_service::<AllocatorMarker>()
            .expect("connect to allocator");

        let mut buffer_constraints = BufferCollectionConstraints {
            min_buffer_count: 2,
            has_buffer_memory_constraints: true,
            ..BUFFER_COLLECTION_CONSTRAINTS_DEFAULT
        };
        buffer_constraints.buffer_memory_constraints.min_size_bytes = 4096;

        let (sender, mut receiver) = futures::channel::oneshot::channel();
        let token_fn = move |token| {
            sender.send(token).expect("should be able to send token");
        };

        let mut allocation =
            SysmemAllocation::allocate(sysmem_client.clone(), buffer_constraints, token_fn)
                .expect("start allocator");

        // Receive the token.  From here on, using the token, the test becomes the other client to
        // the Allocator sharing the memory.
        let token = loop {
            assert!(exec.run_until_stalled(&mut allocation).is_pending());
            if let Poll::Ready(x) = exec.run_until_stalled(&mut receiver) {
                break x;
            }
        };
        let token = token.expect("receive token");

        let (buffer_collection_client, buffer_collection_requests) =
            fidl::endpoints::create_proxy::<BufferCollectionMarker>().expect("proxy creation");
        sysmem_client.bind_shared_collection(token, buffer_collection_requests).expect("bind okay");

        buffer_collection_client
            .set_constraints(true, &mut buffer_constraints)
            .expect("constraints should send okay");

        let allocation_fut = buffer_collection_client.wait_for_buffers_allocated();
        pin_mut!(allocation_fut);

        let (status, buffers) =
            exec.run_singlethreaded(&mut allocation_fut).expect("allocation success");

        assert_eq!(zx::Status::OK.into_raw(), status);

        // Allocator should be ready now.
        let allocated_buffers = match exec.run_until_stalled(&mut allocation) {
            Poll::Ready(bufs) => bufs.expect("allocation success"),
            x => panic!("Expected ready, got {:?}", x),
        };

        let _allocator_settings = allocated_buffers.settings();

        assert_eq!(buffers.buffer_count, allocated_buffers.len());
    }
}
