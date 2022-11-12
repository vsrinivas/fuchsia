// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        errors::FxfsError,
        platform::fuchsia::{errors::map_to_status, file::FxFile, node::OpenedNode},
        round::{round_down, round_up},
    },
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_hardware_block as block,
    fidl_fuchsia_hardware_block_volume::{
        self as volume, VolumeAndNodeMarker, VolumeAndNodeRequest,
    },
    fidl_fuchsia_io as fio,
    fuchsia_async::{self as fasync, FifoReadable, FifoWritable},
    fuchsia_zircon as zx,
    futures::{stream::TryStreamExt, try_join},
    remote_block_device::{BlockFifoRequest, BlockFifoResponse},
    std::{
        collections::{BTreeMap, HashMap},
        convert::TryInto,
        hash::{Hash, Hasher},
        option::Option,
        sync::Mutex,
    },
    vfs::{execution_scope::ExecutionScope, file::File},
};

// Multiple Block I/O request may be sent as a group.
// Notes:
// - the group is identified by `group_id` in the request
// - if using groups, a response will not be sent unless `BLOCKIO_GROUP_LAST`
//   flag is set.
// - when processing a request of a group fails, subsequent requests of that
//   group will not be processed.
//
// Refer to sdk/banjo/fuchsia.hardware.block/block.fidl for details.
//
// FifoMessageGroup keeps track of the relevant BlockFifoResponse field for
// a group requests. Only `status` and `count` needs to be updated.
struct FifoMessageGroup {
    group_id: u16,
    status: zx::sys::zx_status_t,
    count: u32,
}

impl Hash for FifoMessageGroup {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.group_id.hash(state);
    }
}

impl FifoMessageGroup {
    // Initialise a FifoMessageGroup given the request group ID.
    // `count` is set to 0 as no requests has been processed yet.
    fn new(group_id: u16) -> Self {
        Self { group_id, status: zx::sys::ZX_OK, count: 0 }
    }

    // Takes the FifoMessageGroup and converts it to a BlockFifoResponse.
    // Note that this doesn't return the request ID, it needs to be set
    // after extracting the BlockFifoResponse before sending it
    fn into_response(self) -> BlockFifoResponse {
        return BlockFifoResponse {
            status: self.status,
            group_id: self.group_id,
            count: self.count,
            ..Default::default()
        };
    }

    fn increment_count(&mut self) {
        self.count += 1;
    }

    fn set_status(&mut self, status: zx::sys::zx_status_t) {
        self.status = status;
    }

    fn is_err(&self) -> bool {
        self.status != zx::sys::ZX_OK
    }
}

struct FifoMessageGroups(HashMap<u16, FifoMessageGroup>);

// Keeps track of all the group requests that are currently being processed
impl FifoMessageGroups {
    fn new() -> Self {
        Self(HashMap::new())
    }

    // Returns the current MessageGroup with this group ID
    fn get(&mut self, group_id: u16) -> &mut FifoMessageGroup {
        self.0.entry(group_id).or_insert_with(|| FifoMessageGroup::new(group_id))
    }

    // Remove a group when `BLOCKIO_GROUP_LAST` flag is set.
    fn remove(&mut self, group_id: u16) -> FifoMessageGroup {
        match self.0.remove(&group_id) {
            Some(group) => group,
            // `remove(group_id)` can be called when the group has not yet been
            // added to this FifoMessageGroups. In which case, return a default
            // MessageGroup.
            None => FifoMessageGroup::new(group_id),
        }
    }
}

// This is the default slice size used for Volumes in devices
const DEVICE_VOLUME_SLICE_SIZE: u64 = 32 * 1024;

/// Implements server to handle Block requests
pub struct BlockServer {
    file: OpenedNode<FxFile>,
    scope: ExecutionScope,
    server_channel: Option<zx::Channel>,
    maybe_server_fifo: Mutex<Option<zx::Fifo>>,
    message_groups: Mutex<FifoMessageGroups>,
    vmos: Mutex<BTreeMap<u16, zx::Vmo>>,
}

impl BlockServer {
    /// Creates a new BlockServer given a server channel to listen on.
    pub fn new(
        file: OpenedNode<FxFile>,
        scope: ExecutionScope,
        server_channel: zx::Channel,
    ) -> BlockServer {
        BlockServer {
            file,
            scope,
            server_channel: Some(server_channel),
            maybe_server_fifo: Mutex::new(None),
            message_groups: Mutex::new(FifoMessageGroups::new()),
            vmos: Mutex::new(BTreeMap::new()),
        }
    }

    // Returns a VMO id that is currently not being used
    fn get_vmo_id(&self, vmo: zx::Vmo) -> Option<u16> {
        let mut vmos = self.vmos.lock().unwrap();
        let mut prev_id = 0;
        for &id in vmos.keys() {
            if id != prev_id + 1 {
                let vmo_id = prev_id + 1;
                vmos.insert(vmo_id, vmo);
                return Some(vmo_id);
            }
            prev_id = id;
        }
        if prev_id < std::u16::MAX {
            let vmo_id = prev_id + 1;
            vmos.insert(vmo_id, vmo);
            Some(vmo_id)
        } else {
            None
        }
    }

    async fn handle_blockio_write(&self, request: &BlockFifoRequest) -> Result<(), Error> {
        let block_size = self.file.get_block_size();

        let mut data = {
            let vmos = self.vmos.lock().unwrap();
            let vmo = vmos.get(&request.vmoid).ok_or(FxfsError::NotFound)?;
            let mut buffer = vec![0u8; (request.block_count as u64 * block_size) as usize];
            vmo.read(&mut buffer[..], request.vmo_block * block_size)?;
            buffer
        };

        self.file
            .write_at_uncached(request.device_block * block_size as u64, &mut data[..])
            .await?;

        Ok(())
    }

    async fn handle_blockio_read(&self, request: &BlockFifoRequest) -> Result<(), Error> {
        let block_size = self.file.get_block_size();

        let mut buffer = vec![0u8; (request.block_count as u64 * block_size) as usize];
        let bytes_read = self
            .file
            .read_at_uncached(request.device_block * (block_size as u64), &mut buffer[..])
            .await?;

        // Fill in the rest of the buffer if bytes_read is less than the requested amount
        buffer[bytes_read as usize..].fill(0);

        let vmos = self.vmos.lock().unwrap();
        let vmo = vmos.get(&request.vmoid).ok_or(FxfsError::NotFound)?;
        vmo.write(&buffer[..], request.vmo_block * block_size)?;

        Ok(())
    }

    async fn process_fifo_request(&self, request: &BlockFifoRequest) -> zx::sys::zx_status_t {
        fn into_raw_status(result: Result<(), Error>) -> zx::sys::zx_status_t {
            let status: zx::Status = result.map_err(|e| map_to_status(e)).into();
            status.into_raw()
        }

        match request.op_code & remote_block_device::BLOCK_OP_MASK {
            remote_block_device::BLOCKIO_CLOSE_VMO => {
                let mut vmos = self.vmos.lock().unwrap();
                match vmos.remove(&request.vmoid) {
                    Some(_vmo) => zx::sys::ZX_OK,
                    None => zx::sys::ZX_ERR_NOT_FOUND,
                }
            }
            remote_block_device::BLOCKIO_WRITE => {
                into_raw_status(self.handle_blockio_write(&request).await)
            }
            remote_block_device::BLOCKIO_READ => {
                into_raw_status(self.handle_blockio_read(&request).await)
            }
            // TODO(fxbug.dev/89873): simply returning ZX_OK since we're
            // writing to device and no need to flush cache, but need to
            // check that flush goes down the stack
            remote_block_device::BLOCKIO_FLUSH => zx::sys::ZX_OK,
            // TODO(fxbug.dev/89873)
            remote_block_device::BLOCKIO_TRIM => zx::sys::ZX_OK,
            _ => panic!("Unexpected message, request {:?}", request.op_code),
        }
    }

    async fn handle_fifo_request(&self, request: BlockFifoRequest) -> Option<BlockFifoResponse> {
        let is_group = (request.op_code & remote_block_device::BLOCK_GROUP_ITEM) > 0;
        let wants_reply = (request.op_code & remote_block_device::BLOCK_GROUP_LAST) > 0;

        // Set up the BlockFifoResponse for this request, but do no process request yet
        let mut maybe_reply = {
            if is_group {
                let mut groups = self.message_groups.lock().unwrap();
                if wants_reply {
                    let mut group = groups.remove(request.group_id);
                    group.increment_count();

                    // This occurs when a previous request in this group has failed
                    if group.is_err() {
                        let mut reply = group.into_response();
                        reply.request_id = request.request_id;
                        // No need to process this request
                        return Some(reply);
                    }

                    let mut response = group.into_response();
                    response.request_id = request.request_id;
                    Some(response)
                } else {
                    let group = groups.get(request.group_id);
                    group.increment_count();

                    if group.is_err() {
                        // No need to process this request
                        return None;
                    }
                    None
                }
            } else {
                Some(BlockFifoResponse {
                    request_id: request.request_id,
                    count: 1,
                    ..Default::default()
                })
            }
        };
        let status = self.process_fifo_request(&request).await;
        // Status only needs to be updated in the reply if it's not OK
        if status != zx::sys::ZX_OK {
            match &mut maybe_reply {
                None => {
                    // maybe_reply will only be None if it's part of a group request
                    self.message_groups.lock().unwrap().get(request.group_id).set_status(status);
                }
                Some(reply) => {
                    reply.status = status;
                }
            }
        }
        maybe_reply
    }

    fn handle_clone_request(&self, object: ServerEnd<fio::NodeMarker>) {
        let file = OpenedNode::new(self.file.clone());
        let scope_cloned = self.scope.clone();
        self.scope.spawn(async move {
            let mut cloned_server = BlockServer::new(file, scope_cloned, object.into_channel());
            let _ = cloned_server.run().await;
        });
    }

    async fn get_allocated_bytes(&self) -> Result<(u64, u64), zx::Status> {
        let mut allocated_bytes = 0;
        let mut unallocated_bytes = 0;
        let mut offset = 0;

        loop {
            let (allocated, count) = self.file.is_allocated(offset).await?;
            if count == 0 {
                break;
            } else {
                offset += count;
                if allocated {
                    allocated_bytes += count;
                } else {
                    unallocated_bytes += count;
                }
            }
        }
        Ok((allocated_bytes, unallocated_bytes))
    }

    async fn handle_request(&self, request: VolumeAndNodeRequest) -> Result<(), Error> {
        match request {
            VolumeAndNodeRequest::GetInfo { responder } => {
                let block_size = self.file.get_block_size();
                let block_count =
                    (self.file.get_size().await.unwrap() + block_size - 1) / block_size;
                let mut block_info = block::BlockInfo {
                    block_count,
                    block_size: block_size as u32,
                    max_transfer_size: 1024 * 1024,
                    flags: block::Flag::empty(),
                    reserved: 0,
                };
                responder.send(zx::sys::ZX_OK, Some(&mut block_info))?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::GetStats { clear: _, responder } => {
                responder.send(zx::sys::ZX_OK, None)?;
            }
            VolumeAndNodeRequest::GetFifo { responder } => {
                responder.send(zx::sys::ZX_OK, self.maybe_server_fifo.lock().unwrap().take())?;
            }
            VolumeAndNodeRequest::AttachVmo { vmo, responder } => match self.get_vmo_id(vmo) {
                Some(vmo_id) => {
                    responder.send(zx::sys::ZX_OK, Some(&mut block::VmoId { id: vmo_id }))?
                }
                None => responder.send(zx::sys::ZX_ERR_NO_RESOURCES, None)?,
            },
            // TODO(fxbug.dev/89873): close fifo
            VolumeAndNodeRequest::CloseFifo { responder } => {
                responder.send(zx::sys::ZX_OK)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::RebindDevice { responder } => {
                responder.send(zx::sys::ZX_OK)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::ReadBlocks {
                responder,
                vmo: _,
                length: _,
                dev_offset: _,
                vmo_offset: _,
            } => {
                responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::WriteBlocks {
                responder,
                vmo: _,
                length: _,
                dev_offset: _,
                vmo_offset: _,
            } => {
                responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::GetTypeGuid { responder } => {
                responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED, None)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::GetInstanceGuid { responder } => {
                responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED, None)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::GetName { responder } => {
                responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED, None)?;
            }
            VolumeAndNodeRequest::QuerySlices { start_slices, responder } => {
                // Initialise slices with default value. `slices` will be converted to an array
                // of size volume::MAX_SLICE_REQUESTS
                let mut slices = vec![
                    volume::VsliceRange { allocated: false, count: 0 };
                    volume::MAX_SLICE_REQUESTS as usize
                ];

                let mut status = zx::sys::ZX_OK;
                let mut response_count = 0;
                for (slice, start_slice) in slices.iter_mut().zip(start_slices.into_iter()) {
                    match self.file.is_allocated(start_slice * DEVICE_VOLUME_SLICE_SIZE).await {
                        Ok((allocated, bytes)) => {
                            slice.count = round_up(bytes, DEVICE_VOLUME_SLICE_SIZE).unwrap();
                            slice.allocated = allocated;
                            response_count += 1;
                        }
                        Err(e) => {
                            status = e.into_raw();
                            break;
                        }
                    }
                }

                // Create a vector with the mutable reference to each element in the vector
                // `slices`, then convert this vector into an array to send to the responder
                let mut response = slices.iter_mut().collect::<Vec<_>>().try_into().unwrap();
                responder.send(status, &mut response, response_count)?;
            }
            // TODO(fxbug.dev/89873): need to check if this returns the right information.
            VolumeAndNodeRequest::GetVolumeInfo { responder } => {
                match self.get_allocated_bytes().await {
                    Ok((allocated_bytes, unallocated_bytes)) => {
                        let allocated_slices =
                            round_up(allocated_bytes, DEVICE_VOLUME_SLICE_SIZE).unwrap();
                        let unallocated_bytes =
                            round_down(unallocated_bytes, DEVICE_VOLUME_SLICE_SIZE);
                        let mut manager = volume::VolumeManagerInfo {
                            slice_size: DEVICE_VOLUME_SLICE_SIZE,
                            slice_count: allocated_slices + unallocated_bytes,
                            assigned_slice_count: allocated_slices,
                            maximum_slice_count: allocated_slices + unallocated_bytes,
                            max_virtual_slice: allocated_slices + unallocated_bytes,
                        };
                        let mut volume_info = volume::VolumeInfo {
                            partition_slice_count: allocated_slices,
                            slice_limit: 0,
                        };
                        responder.send(
                            zx::sys::ZX_OK,
                            Some(&mut manager),
                            Some(&mut volume_info),
                        )?;
                    }
                    Err(e) => {
                        responder.send(e.into_raw(), None, None)?;
                    }
                }
            }
            VolumeAndNodeRequest::Extend { start_slice, slice_count, responder } => {
                // TODO(fxbug.dev/89873): this is a hack. When extend is called, the extent is
                // expected to be set as allocated. The easiest way to do this is to just
                // write an extent of zeroed data. Another issue here is the size. The memory
                // allocated here should be bounded to what's available.
                let data = vec![0u8; (slice_count * DEVICE_VOLUME_SLICE_SIZE) as usize];
                match self
                    .file
                    .write_at_uncached(start_slice * DEVICE_VOLUME_SLICE_SIZE, data[..].into())
                    .await
                {
                    Ok(_) => responder.send(zx::sys::ZX_OK)?,
                    Err(status) => responder.send(status.into_raw())?,
                };
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::Shrink { start_slice: _, slice_count: _, responder } => {
                responder.send(zx::sys::ZX_OK)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::Destroy { responder } => {
                responder.send(zx::sys::ZX_OK)?;
            }
            VolumeAndNodeRequest::Clone { flags: _, object, control_handle: _ } => {
                // Have to move this into a non-async function to avoid Rust compiler's
                // complaint about recursive async functions
                self.handle_clone_request(object);
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::Reopen {
                rights_request: _,
                object_request: _,
                control_handle: _,
            } => {}
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::Close { responder } => {
                responder.send(&mut Ok(()))?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::DescribeDeprecated { responder } => {
                let mut info = fio::NodeInfoDeprecated::Service(fio::Service);
                responder.send(&mut info)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::GetConnectionInfo { responder } => {
                // TODO(https://fxbug.dev/77623): Fill in rights and available operations.
                let info = fio::ConnectionInfo { ..fio::ConnectionInfo::EMPTY };
                responder.send(info)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::Sync { responder } => {
                responder.send(&mut Err(zx::sys::ZX_ERR_NOT_SUPPORTED))?;
            }
            VolumeAndNodeRequest::GetAttr { responder } => {
                match self.file.get_attrs().await {
                    Ok(mut attrs) => {
                        attrs.mode = fio::MODE_TYPE_BLOCK_DEVICE;
                        responder.send(zx::sys::ZX_OK, &mut attrs)?;
                    }
                    Err(e) => {
                        let mut attrs = fio::NodeAttributes {
                            mode: 0,
                            id: fio::INO_UNKNOWN,
                            content_size: 0,
                            storage_size: 0,
                            link_count: 0,
                            creation_time: 0,
                            modification_time: 0,
                        };
                        responder.send(e.into_raw(), &mut attrs)?;
                    }
                };
            }
            // TODO(fxbug.dev/89873) VolumeAndNodeRequest::GetAttributes { query, responder }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::SetAttr { flags: _, attributes: _, responder } => {
                responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::GetAttributes { query: _, responder } => {
                responder.send(&mut Err(zx::sys::ZX_ERR_NOT_SUPPORTED))?;
            }
            VolumeAndNodeRequest::UpdateAttributes { payload: _, responder } => {
                responder.send(&mut Err(zx::sys::ZX_ERR_NOT_SUPPORTED))?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::GetFlags { responder } => {
                responder.send(zx::sys::ZX_OK, fio::OpenFlags::NODE_REFERENCE)?;
            }
            // TODO(fxbug.dev/89873)
            VolumeAndNodeRequest::SetFlags { flags: _, responder } => {
                responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED)?;
            }
            // TODO(fxbug.dev/105608)
            VolumeAndNodeRequest::Query { responder } => {
                responder.send(fio::NODE_PROTOCOL_NAME.as_bytes())?;
            }
            VolumeAndNodeRequest::QueryFilesystem { responder } => {
                match self.file.query_filesystem() {
                    Ok(mut info) => responder.send(zx::sys::ZX_OK, Some(&mut info))?,
                    Err(e) => responder.send(e.into_raw(), None)?,
                }
            }
        }
        Ok(())
    }

    async fn handle_requests(
        &self,
        server: fidl::endpoints::ServerEnd<VolumeAndNodeMarker>,
    ) -> Result<(), Error> {
        server
            .into_stream()?
            .map_err(|e| e.into())
            .try_for_each(|request| self.handle_request(request))
            .await?;
        Ok(())
    }

    pub async fn run(&mut self) -> Result<(), Error> {
        let server = ServerEnd::<VolumeAndNodeMarker>::new(self.server_channel.take().unwrap());

        // Create a fifo pair
        let (server_fifo, client_fifo) =
            zx::Fifo::create(16, std::mem::size_of::<BlockFifoRequest>())?;
        self.maybe_server_fifo = Mutex::new(Some(client_fifo));

        // Handling requests from fifo
        let fifo_future = async {
            let fifo = fasync::Fifo::<BlockFifoRequest, BlockFifoResponse>::from_fifo(server_fifo)?;
            loop {
                match fifo.read_entry().await {
                    Ok(request) => {
                        if let Some(response) = self.handle_fifo_request(request).await {
                            fifo.write_entries(std::slice::from_ref(&response)).await?;
                        }
                        // if `self.handle_fifo_request(..)` returns None, then
                        // there's no reply for this request. This occurs for
                        // requests part of a group request where
                        // `BLOCK_GROUP_LAST` flag is not set.
                    }
                    Err(zx::Status::PEER_CLOSED) => break Result::<_, Error>::Ok(()),
                    Err(e) => break Err(e.into()),
                }
            }
        };

        // Handling requests from fidl
        let channel_future = async {
            self.handle_requests(server).await?;
            // This is temporary for when client doesn't call for fifo
            self.maybe_server_fifo.lock().unwrap().take();
            Ok(())
        };

        try_join!(fifo_future, channel_future)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::platform::fuchsia::testing::{open_file_checked, TestFixture},
        fidl::endpoints::{ClientEnd, ServerEnd},
        fidl_fuchsia_hardware_block_volume::VolumeAndNodeMarker,
        fidl_fuchsia_io as fio,
        fs_management::{filesystem::Filesystem, Blobfs},
        fuchsia_async as fasync,
        fuchsia_merkle::MerkleTree,
        fuchsia_zircon as zx,
        futures::join,
        remote_block_device::{BlockClient, RemoteBlockClient, VmoId},
        std::collections::HashSet,
    };

    #[fasync::run(10, test)]
    async fn test_block_server() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        join!(
            async {
                let mut blobfs = Filesystem::from_channel(client_channel, Blobfs::default())
                    .expect("create filesystem from channel failed");
                blobfs.format().await.expect("format blobfs failed");
                blobfs.fsck().await.expect("fsck failed");
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();

                let file = open_file_checked(
                    &root,
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_FILE,
                    "block_device",
                )
                .await;
                let () = file
                    .resize(2 * 1024 * 1024)
                    .await
                    .expect("resize failed")
                    .map_err(zx::Status::from_raw)
                    .expect("resize error");
                let () = file
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(zx::Status::from_raw)
                    .expect("close error");

                root.open(
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "block_device",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_clone() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        let (client_channel_copy1, server_channel_copy1) =
            zx::Channel::create().expect("Channel::create failed");
        let (client_channel_copy2, server_channel_copy2) =
            zx::Channel::create().expect("Channel::create failed");

        join!(
            async {
                // Putting original block device in its own execution scope to test that the
                // clone will work independent of the original
                {
                    let original_block_device =
                        ClientEnd::<VolumeAndNodeMarker>::new(client_channel)
                            .into_proxy()
                            .expect("convert into proxy failed");
                    original_block_device
                        .clone(
                            fio::OpenFlags::CLONE_SAME_RIGHTS,
                            ServerEnd::new(server_channel_copy1),
                        )
                        .expect("clone failed");
                    original_block_device
                        .clone(
                            fio::OpenFlags::CLONE_SAME_RIGHTS,
                            ServerEnd::new(server_channel_copy2),
                        )
                        .expect("clone failed");
                }

                let block_device_cloned1 = RemoteBlockClient::new(client_channel_copy1)
                    .await
                    .expect("create new RemoteBlockClient failed");
                let block_device_cloned2 = RemoteBlockClient::new(client_channel_copy2)
                    .await
                    .expect("create new RemoteBlockClient failed");

                let offset = block_device_cloned1.block_size() as usize;
                let len = block_device_cloned1.block_size() as usize;
                // Must write with length as a multiple of the block_size
                let write_buf = vec![0xa3u8; len];
                // Write to "foo" via block_device_cloned1
                block_device_cloned1
                    .write_at(write_buf[..].into(), offset as u64)
                    .await
                    .expect("write_at failed");
                let mut read_buf = vec![0u8; len];
                block_device_cloned2
                    .read_at(read_buf.as_mut_slice().into(), offset as u64)
                    .await
                    .expect("read_at failed");
                assert_eq!(&read_buf, &write_buf);
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_attach_vmo() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client_channel)
                    .await
                    .expect("RemoteBlockClient::new failed");
                let mut vmo_set = HashSet::new();
                let vmo = zx::Vmo::create(1).expect("Vmo::create failed");
                for _ in 1..5 {
                    match remote_block_device.attach_vmo(&vmo).await {
                        Ok(vmo_id) => {
                            // TODO(fxbug.dev/89873): need to detach vmoid. into_id() is a
                            // temporary solution. Remove this after detaching vmo has been
                            // implemented
                            // Make sure that vmo_id is unique
                            assert_eq!(vmo_set.insert(vmo_id.into_id()), true);
                        }
                        Err(e) => panic!("unexpected error {:?}", e),
                    }
                }
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_detach_vmo() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client_channel)
                    .await
                    .expect("RemoteBlockClient::new failed");
                let vmo = zx::Vmo::create(1).expect("Vmo::create failed");
                let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");
                let vmo_id_copy = VmoId::new(vmo_id.id());
                remote_block_device.detach_vmo(vmo_id).await.expect("detach failed");
                remote_block_device.detach_vmo(vmo_id_copy).await.expect_err("detach succeeded");
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_read_write_files() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client_channel)
                    .await
                    .expect("RemoteBlockClient::new failed");
                let vmo = zx::Vmo::create(131072).expect("create vmo failed");
                let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");

                // Must write with length as a multiple of the block_size
                let offset = remote_block_device.block_size() as usize;
                let len = remote_block_device.block_size() as usize;
                let write_buf = vec![0xa3u8; len];
                remote_block_device
                    .write_at(write_buf[..].into(), offset as u64)
                    .await
                    .expect("write_at failed");

                // Read back an extra block either side
                let mut read_buf = vec![0u8; len + 2 * remote_block_device.block_size() as usize];
                remote_block_device
                    .read_at(
                        read_buf.as_mut_slice().into(),
                        offset as u64 - remote_block_device.block_size() as u64,
                    )
                    .await
                    .expect("read_at failed");

                // We expect the extra block on the LHS of the read_buf to be 0
                assert_eq!(&read_buf[..offset], &vec![0; offset][..]);
                assert_eq!(&read_buf[offset..offset + len], &write_buf);
                // We expect the extra block on the RHS of the read_buf to be 0
                assert_eq!(
                    &read_buf[offset + len..],
                    &vec![0; remote_block_device.block_size() as usize][..]
                );

                remote_block_device.detach_vmo(vmo_id).await.expect("detach failed");
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_flush_is_called() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client_channel)
                    .await
                    .expect("RemoteBlockClient::new failed");
                remote_block_device.flush().await.expect("flush failed");
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_getattr() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");

        join!(
            async {
                let original_block_device = ClientEnd::<VolumeAndNodeMarker>::new(client_channel)
                    .into_proxy()
                    .expect("convert into proxy failed");
                let (status, attr) =
                    original_block_device.get_attr().await.expect("get_attr failed");
                zx::Status::ok(status).expect("block get_attr failed");
                assert_eq!(attr.mode, fio::MODE_TYPE_BLOCK_DEVICE);
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();
                root.open(
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "foo",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_get_info() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        let file_size = 2 * 1024 * 1024;
        join!(
            async {
                let original_block_device = ClientEnd::<VolumeAndNodeMarker>::new(client_channel)
                    .into_proxy()
                    .expect("convert into proxy failed");
                let (status, maybe_info) =
                    original_block_device.get_info().await.expect("get_info failed");
                zx::Status::ok(status).expect("block get_info failed");
                let info = maybe_info.expect("block get_info failed");
                assert_eq!(info.block_count * info.block_size as u64, file_size);
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();

                let file = open_file_checked(
                    &root,
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_FILE,
                    "block_device",
                )
                .await;
                let () = file
                    .resize(file_size)
                    .await
                    .expect("resize failed")
                    .map_err(zx::Status::from_raw)
                    .expect("resize error");
                let () = file
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(zx::Status::from_raw)
                    .expect("close error");

                root.open(
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "block_device",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }

    #[fasync::run(10, test)]
    async fn test_blobfs() {
        let (client_channel, server_channel) =
            zx::Channel::create().expect("Channel::create failed");
        join!(
            async {
                let mut blobfs = Filesystem::from_channel(client_channel, Blobfs::default())
                    .expect("create filesystem from channel failed");
                blobfs.format().await.expect("format blobfs failed");
                blobfs.fsck().await.expect("fsck failed");
                // Mount blobfs
                let serving = blobfs.serve().await.expect("serve blobfs failed");

                let content = String::from("Hello world!").into_bytes();
                let merkle_root_hash =
                    MerkleTree::from_reader(&content[..]).unwrap().root().to_string();
                {
                    let file = fuchsia_fs::directory::open_file(
                        serving.root(),
                        &merkle_root_hash,
                        fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
                    )
                    .await
                    .expect("open file failed");
                    let () = file
                        .resize(content.len() as u64)
                        .await
                        .expect("resize failed")
                        .map_err(zx::Status::from_raw)
                        .expect("resize error");
                    let _: u64 = file
                        .write(&content)
                        .await
                        .expect("write to file failed")
                        .map_err(zx::Status::from_raw)
                        .expect("write to file error");
                }
                // Check that blobfs can be successfully unmounted
                serving.shutdown().await.expect("shutdown blobfs failed");

                let serving = blobfs.serve().await.expect("serve blobfs failed");
                {
                    let file = fuchsia_fs::directory::open_file(
                        serving.root(),
                        &merkle_root_hash,
                        fio::OpenFlags::RIGHT_READABLE,
                    )
                    .await
                    .expect("open file failed");
                    let read_content =
                        fuchsia_fs::file::read(&file).await.expect("read from file failed");
                    assert_eq!(content, read_content);
                }

                serving.shutdown().await.expect("shutdown blobfs failed");
            },
            async {
                let fixture = TestFixture::new().await;
                let root = fixture.root();

                let file = open_file_checked(
                    &root,
                    fio::OpenFlags::CREATE
                        | fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_FILE,
                    "block_device",
                )
                .await;
                let () = file
                    .resize(5 * 1024 * 1024)
                    .await
                    .expect("resize failed")
                    .map_err(zx::Status::from_raw)
                    .expect("resize error");
                let () = file
                    .close()
                    .await
                    .expect("close failed")
                    .map_err(zx::Status::from_raw)
                    .expect("close error");

                root.open(
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_BLOCK_DEVICE,
                    "block_device",
                    ServerEnd::new(server_channel),
                )
                .expect("open failed");

                fixture.close().await;
            }
        );
    }
}
