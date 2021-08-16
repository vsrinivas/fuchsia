// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util::MappedVmo,
    anyhow::{anyhow, Context, Error},
    fidl::HandleBased,
    fidl_fuchsia_hardware_nand::Info as NandInfo,
    fidl_fuchsia_nand::{BrokerProxy, BrokerRequestData, BrokerRequestDataBytes},
    fidl_fuchsia_nand_flashmap::{FlashmapRequest, FlashmapRequestStream},
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, TryStreamExt},
    std::{
        collections::HashMap,
        convert::{TryFrom, TryInto},
        ffi::CStr,
    },
};

#[repr(C, packed)]
#[derive(Clone, Copy, Debug, PartialEq)]
struct FlashmapHeader {
    signature: [u8; 8],
    ver_major: u8,
    ver_minor: u8,
    base: u64,
    size: u32,
    name: [u8; 32],
    nareas: u16,
}

impl Default for FlashmapHeader {
    fn default() -> Self {
        let mut ret = FlashmapHeader {
            signature: [0; 8],
            ver_major: FLASHMAP_MAJOR_VER,
            ver_minor: FLASHMAP_MINOR_VER,
            base: 0,
            size: 0,
            name: [0; 32],
            nareas: 0,
        };

        ret.signature.copy_from_slice(FLASHMAP_MAGIC.as_bytes());
        ret
    }
}

const FLASHMAP_MAGIC: &str = "__FMAP__";
const FLASHMAP_MAJOR_VER: u8 = 1;
const FLASHMAP_MINOR_VER: u8 = 1;

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
struct RawFlashmapArea {
    offset: u32,
    size: u32,
    name: [u8; 32],
    flags: u16,
}

impl RawFlashmapArea {
    #[cfg(test)]
    pub fn new(name: &str, offset: u32, size: u32) -> Self {
        let mut ret = RawFlashmapArea { offset, size, name: [0; 32], flags: 0 };
        ret.name[0..name.as_bytes().len()].copy_from_slice(name.as_bytes());
        ret
    }
}

/// The same as |RawFlashmapArea|, except |name| is a Rust string.
#[derive(Clone, Debug)]
struct FlashmapArea {
    offset: u32,
    size: u32,
    #[allow(dead_code)]
    flags: u16,
    name: String,
}

impl TryFrom<&RawFlashmapArea> for FlashmapArea {
    type Error = anyhow::Error;
    fn try_from(area: &RawFlashmapArea) -> Result<FlashmapArea, Error> {
        let len = match area.name.iter().position(|c| *c == 0) {
            None => return Err(anyhow!("Name was not null-terminated.")),
            Some(len) => len,
        };

        let string = CStr::from_bytes_with_nul(&area.name[0..len + 1]).context("Bad string")?;
        Ok(FlashmapArea {
            offset: area.offset,
            size: area.size,
            flags: area.flags,
            name: string.to_owned().into_string().context("Making string")?,
        })
    }
}

pub struct Flashmap {
    areas: HashMap<String, FlashmapArea>,
    info: NandInfo,
    device: BrokerProxy,
    vmo_size: usize,
    inner: Mutex<FlashmapInner>,
}

struct FlashmapInner {
    nand_vmo: zx::Vmo,
    mapping: MappedVmo,
}

impl Flashmap {
    /// Construct a new |Flashmap|. Will return an error if the given NAND device |device| doesn't
    /// contain a flashmap.
    pub async fn new(device: BrokerProxy, address: Option<u64>) -> Result<Self, Error> {
        let (status, info) = device.get_info().await.context("Sending FIDL get_info request")?;
        zx::ok(status).context("getting info failed")?;
        let info = info.unwrap();

        let vmo_size = 4 * zx::system_get_page_size();
        let nand_vmo = zx::Vmo::create(vmo_size as u64).context("Creating VMO")?;
        let mapping = MappedVmo::new(&nand_vmo, vmo_size as usize).context("Mapping NAND VMO")?;

        let mut map = Flashmap {
            areas: HashMap::new(),
            info: *info,
            device,
            vmo_size: vmo_size as usize,
            inner: Mutex::new(FlashmapInner { nand_vmo, mapping }),
        };

        map.find_flashmap(address).await?;

        Ok(map)
    }

    /// Perform a linear search on the NAND device to find the flashmap header.
    async fn find_flashmap(&mut self, address: Option<u64>) -> Result<(), Error> {
        let max_offset = (self.info.pages_per_block as u32) * (self.info.num_blocks as u32);
        if self.vmo_size % (self.info.page_size as usize) != 0 {
            return Err(anyhow!("Page size must divide evenly into VMO size."));
        }

        let inner = self.inner.get_mut();

        let pages_per_vmo = self.vmo_size / (self.info.page_size as usize);

        let data = inner.mapping.as_slice();

        let (mut read_start_page, should_scan) = match address {
            None => (0, true),
            Some(value) => (value / (self.info.page_size as u64))
                .try_into()
                .map(|v| (v, false))
                .unwrap_or_else(|_| {
                    fx_log_warn!(
                        "Suggested address is too large, falling back to a linear search."
                    );
                    (0, true)
                }),
        };

        let mut request = BrokerRequestData {
            vmo: None,
            length: pages_per_vmo as u32,
            data_vmo: true,
            offset_data_vmo: 0,
            offset_oob_vmo: 0,
            offset_nand: read_start_page,
            oob_vmo: false,
        };

        // Scan through the NAND, checking to see if each chunk contains the flashmap header.
        while read_start_page < max_offset {
            request.vmo = Some(
                inner
                    .nand_vmo
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .context("duplicating VMO")?,
            );
            request.length =
                std::cmp::min(pages_per_vmo as u32, (max_offset - read_start_page) as u32);
            let (status, _) = self.device.read(&mut request).await.context("Sending NAND read")?;
            zx::ok(status).context("reading failed")?;

            read_start_page += request.length;
            if let Some((header, index)) = Self::find_flashmap_header(
                &data[0..(request.length * self.info.page_size) as usize],
            ) {
                // This is probably the real deal.
                // We should read in the header.
                let header_copy = *header;
                self.load_flashmap(&mut request, index, header_copy)
                    .await
                    .context("Failed to read flashmap")?;
                return Ok(());
            } else {
                request.offset_nand += request.length;
            }

            if !should_scan {
                break;
            }
        }

        Err(anyhow!("Could not find flashmap header."))
    }

    /// Scan through |data| for a flashmap header.
    /// If the header starts at the end of |data| and continues into the next chunk, it will not be
    /// found.
    fn find_flashmap_header<'a>(data: &'a [u8]) -> Option<(&'a FlashmapHeader, usize)> {
        for i in 0..data.len() - std::mem::size_of::<FlashmapHeader>() {
            if &data[i..i + FLASHMAP_MAGIC.as_bytes().len()] != FLASHMAP_MAGIC.as_bytes() {
                continue;
            }

            // Safe because we will always have at least |size_of(FlashmapHeader)| bytes left in the array.
            let header: &FlashmapHeader = unsafe { std::mem::transmute(&data[i]) };
            if header.ver_major != FLASHMAP_MAJOR_VER || header.ver_minor != FLASHMAP_MINOR_VER {
                continue;
            }

            // Make sure the name is null-terminated.
            if !header.name.iter().any(|c| *c == 0) {
                continue;
            }

            return Some((header, i));
        }

        None
    }

    /// Load the flashmap and populate the list of flashmap areas.
    /// |request| should be the request that was last used to read from the NAND.
    /// |offset_in_vmar| should be the offset of the FlashmapHeader within the VMO.
    /// |header| is the flashmap header.
    async fn load_flashmap(
        &mut self,
        request: &mut BrokerRequestData,
        mut offset_in_vmar: usize,
        header: FlashmapHeader,
    ) -> Result<(), Error> {
        let inner = self.inner.get_mut();
        let data = inner.mapping.as_slice();
        let data_left = data.len() - offset_in_vmar;
        let data_needed = std::mem::size_of::<FlashmapHeader>()
            + ((header.nareas as usize) * std::mem::size_of::<RawFlashmapArea>());

        // Account for data that comes at the start of the page, before the flashmap header starts.
        // We don't care about it, but we need room in the VMO to read it.
        let padding_in_page = offset_in_vmar % (self.info.page_size as usize);
        if padding_in_page + data_needed > data.len() {
            return Err(anyhow!("Flashmap is bigger than VMO size!"));
        }

        // Check that we read the whole flashmap, and not just the header.
        if data_needed > data_left {
            // Adjust the request, and do another read.
            request.offset_nand += (offset_in_vmar as u32) / self.info.page_size;
            request.length = (data_needed as u32) / self.info.page_size;
            // Update where the FlashmapHeader should be in the vmar.
            // We'll check this below.
            offset_in_vmar %= self.info.page_size as usize;
            if data_needed % (self.info.page_size as usize) != 0 {
                request.length += 1;
            }
            request.vmo = Some(
                inner
                    .nand_vmo
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .context("duplicating VMO")?,
            );

            let (status, _) = self.device.read(request).await.context("Sending NAND read")?;
            zx::ok(status).context("reading failed")?;
        }

        // Safe because we will always have at least |size_of(FlashmapHeader)| bytes left in the
        // array.
        let header: &FlashmapHeader = unsafe { std::mem::transmute(&data[offset_in_vmar]) };
        if header.signature != FLASHMAP_MAGIC.as_bytes() {
            // This should never happen.
            return Err(anyhow!("Flashmap magic was not found?"));
        }

        // Read in the areas, which are found immediately after the header.
        let area_start = offset_in_vmar + std::mem::size_of::<FlashmapHeader>();
        let areas_ptr: *const RawFlashmapArea = unsafe { std::mem::transmute(&data[area_start]) };
        // Safe because we make sure we have at least |data_needed| bytes to read above.
        let areas = unsafe { std::slice::from_raw_parts(areas_ptr, header.nareas as usize) };

        for raw_area in areas {
            let area = FlashmapArea::try_from(raw_area)?;
            self.areas.insert(area.name.clone(), area);
        }

        Ok(())
    }

    /// Read from area |name| at offset |offset| with size |size|.
    async fn read_flashmap_area(
        &self,
        name: &str,
        offset: u32,
        size: u32,
    ) -> Result<fidl_fuchsia_mem::Range, zx::Status> {
        let area = self.areas.get(name).ok_or(zx::Status::NOT_FOUND)?;
        if offset.checked_add(size).ok_or(zx::Status::OUT_OF_RANGE)? > area.size {
            fx_log_warn!(
                "Read {} at {:x} size={:x} is larger than area size {:x}",
                name,
                offset,
                size,
                area.size
            );
            return Err(zx::Status::OUT_OF_RANGE);
        }

        // Figure out the page and the offset within the page.
        let page = (area.offset + offset) / self.info.page_size;
        let offset_in_page = (area.offset + offset) % self.info.page_size;

        // Figure out how much we're going to read, including the padding between the start of the
        // page and the start of the area.
        let mut pages_to_read = (size + offset_in_page) / self.info.page_size;
        if (size + offset_in_page) % self.info.page_size != 0 {
            pages_to_read += 1;
        }

        let vmo = zx::Vmo::create((pages_to_read * self.info.page_size) as u64)?;
        let vmo_dup = vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?;

        let mut request = BrokerRequestData {
            vmo: Some(vmo_dup),
            length: pages_to_read,
            data_vmo: true,
            offset_data_vmo: 0,
            offset_oob_vmo: 0,
            offset_nand: page,
            oob_vmo: false,
        };

        let (status, _) = self.device.read(&mut request).await.map_err(|e| {
            fx_log_warn!("Send read failed: {:?}", e);
            zx::Status::INTERNAL
        })?;
        zx::ok(status)?;

        Ok(fidl_fuchsia_mem::Range { vmo, offset: offset_in_page as u64, size: size as u64 })
    }

    async fn write_flashmap_area(
        &self,
        name: &str,
        offset: u32,
        data: fidl_fuchsia_mem::Buffer,
    ) -> Result<(), zx::Status> {
        let area = self.areas.get(name).ok_or(zx::Status::NOT_FOUND)?;
        if offset
            .checked_add(data.size.try_into().map_err(|_| zx::Status::OUT_OF_RANGE)?)
            .ok_or(zx::Status::OUT_OF_RANGE)?
            > area.size
        {
            fx_log_warn!(
                "Write {} at {:x} size={:x} is larger than area size {:x}",
                name,
                offset,
                data.size,
                area.size
            );
            return Err(zx::Status::OUT_OF_RANGE);
        }

        let offset_nand = area.offset + offset;

        let mut request = BrokerRequestDataBytes {
            vmo: data.vmo,
            offset_data_vmo: 0,
            length: data.size,
            offset_nand: offset_nand as u64,
        };

        let status = self.device.write_bytes(&mut request).await.map_err(|e| {
            fx_log_warn!("Send write failed: {:?}", e);
            zx::Status::INTERNAL
        })?;
        zx::ok(status)?;

        Ok(())
    }

    async fn erase_flashmap_area(
        &self,
        name: &str,
        offset: u32,
        range: u32,
    ) -> Result<(), zx::Status> {
        let area = self.areas.get(name).ok_or(zx::Status::NOT_FOUND)?;
        if offset.checked_add(range).ok_or(zx::Status::OUT_OF_RANGE)? > area.size {
            fx_log_warn!(
                "Erase {} at {:x} size={:x} is larger than area size {:x}",
                name,
                offset,
                range,
                area.size
            );
            return Err(zx::Status::OUT_OF_RANGE);
        }

        let erase_block_size = self.info.page_size * self.info.pages_per_block;
        // Make sure that the start address and size is aligned to an erase block.
        let erase_start_byte = area.offset + offset;
        if erase_start_byte % erase_block_size != 0 {
            return Err(zx::Status::INVALID_ARGS);
        }
        if range % erase_block_size != 0 {
            return Err(zx::Status::INVALID_ARGS);
        }

        let erase_start_block = erase_start_byte / erase_block_size;
        let erase_range_blocks = range / erase_block_size;

        let mut request = BrokerRequestData {
            length: erase_range_blocks,
            offset_nand: erase_start_block,
            vmo: None,
            offset_data_vmo: 0,
            offset_oob_vmo: 0,
            data_vmo: false,
            oob_vmo: false,
        };

        let status = self.device.erase(&mut request).await.map_err(|e| {
            fx_log_warn!("Send erase failed: {:?}", e);
            zx::Status::INTERNAL
        })?;
        zx::ok(status)?;

        Ok(())
    }

    /// Handle fuchsia.nand.flashmap/Flashmap requests coming in on |stream|.
    pub async fn serve(&self, mut stream: FlashmapRequestStream) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await.context("Getting next request")? {
            match req {
                FlashmapRequest::GetAreas { responder } => {
                    let mut fidl_areas = self
                        .areas
                        .iter()
                        .map(|(_, area)| fidl_fuchsia_nand_flashmap::Area {
                            name: area.name.clone(),
                            size: area.size,
                            flags: fidl_fuchsia_nand_flashmap::AreaFlags::from_bits_allow_unknown(
                                area.flags,
                            ),
                            offset: area.offset,
                        })
                        .collect::<Vec<fidl_fuchsia_nand_flashmap::Area>>();

                    fidl_areas.sort_by(|a, b| a.offset.cmp(&b.offset));

                    responder.send(&mut fidl_areas.iter_mut()).context("Replying to GetAreas")?;
                }
                FlashmapRequest::GetEraseBlockSize { responder } => {
                    responder
                        .send(self.info.page_size * self.info.pages_per_block)
                        .context("Responding to get erase block size")?;
                }
                FlashmapRequest::Read { name, offset, size, responder } => {
                    responder
                        .send(
                            &mut self
                                .read_flashmap_area(&name, offset, size)
                                .await
                                .map_err(|e| e.into_raw()),
                        )
                        .context("Responding to flashmap read")?;
                }
                FlashmapRequest::Write { name, offset, data, responder } => {
                    responder
                        .send(
                            &mut self
                                .write_flashmap_area(&name, offset, data)
                                .await
                                .map_err(|e| e.into_raw()),
                        )
                        .context("Responding to flashmap write")?;
                }
                FlashmapRequest::Erase { name, offset, range, responder } => {
                    responder
                        .send(
                            &mut self
                                .erase_flashmap_area(&name, offset, range)
                                .await
                                .map_err(|e| e.into_raw()),
                        )
                        .context("Responding to flashmap erase")?;
                }
            }
        }

        Ok(())
    }
}

#[cfg(test)]
pub mod tests {

    use {
        super::*,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_device::{ControllerRequest, ControllerRequestStream},
        fidl_fuchsia_nand::{BrokerMarker, BrokerRequest, BrokerRequestStream},
        fuchsia_async::Task,
        std::sync::Arc,
    };

    fn make_slice<T>(val: &T) -> &[u8] {
        unsafe {
            std::slice::from_raw_parts((val as *const T) as *const u8, std::mem::size_of::<T>())
        }
    }

    #[derive(Default, Clone, Copy)]
    pub struct Stats {
        pub get_info: usize,
        pub reads: usize,
        pub writes: usize,
        pub erases: usize,
    }

    const PAGE_SIZE: u32 = 32;
    const PAGES_PER_BLOCK: u32 = 4;
    pub struct FakeNandDevice {
        data: Mutex<Vec<u8>>,
        stats: Mutex<Stats>,
    }

    impl FakeNandDevice {
        pub fn new(blocks: u32) -> Self {
            let mut data: Vec<u8> = Vec::new();
            data.resize((blocks * PAGES_PER_BLOCK * PAGE_SIZE) as usize, 0xff);

            FakeNandDevice { data: Mutex::new(data), stats: Mutex::new(Default::default()) }
        }

        pub fn new_with_flashmap() -> Self {
            let size_bytes = 4 * PAGES_PER_BLOCK * PAGE_SIZE;
            let mut data: Vec<u8> = Vec::new();
            data.resize(size_bytes as usize, 0xff);

            let mut header = FlashmapHeader::default();
            header.nareas = 1;

            let area = RawFlashmapArea::new("HELLO", 0, size_bytes);

            let mut pos = 0;
            let slice = make_slice(&header);
            data[pos..pos + slice.len()].copy_from_slice(slice);
            pos += slice.len();

            let slice = make_slice(&area);
            data[pos..pos + slice.len()].copy_from_slice(slice);

            FakeNandDevice { data: Mutex::new(data), stats: Mutex::new(Default::default()) }
        }

        /// Serve a single Controller GetTopologicalPath request before handling Broker requests.
        pub async fn serve_topo_path(
            &self,
            mut stream: ControllerRequestStream,
            path: String,
        ) -> Result<(), Error> {
            while let Some(req) = stream.try_next().await? {
                match req {
                    ControllerRequest::GetTopologicalPath { responder } => {
                        responder.send(&mut Ok(path.clone())).unwrap();
                        break;
                    }
                    _ => unimplemented!(),
                }
            }

            let (inner, terminated) = stream.into_inner();
            let broker = BrokerRequestStream::from_inner(inner, terminated);
            self.serve(broker).await
        }

        pub async fn serve(&self, mut stream: BrokerRequestStream) -> Result<(), Error> {
            while let Some(req) = stream.try_next().await? {
                match req {
                    BrokerRequest::GetInfo { responder } => {
                        self.stats.lock().await.get_info += 1;
                        let mut info = NandInfo {
                            page_size: PAGE_SIZE,
                            ecc_bits: 0,
                            oob_size: 0,
                            pages_per_block: PAGES_PER_BLOCK,
                            num_blocks: (self.data.lock().await.len()
                                / ((PAGE_SIZE * PAGES_PER_BLOCK) as usize))
                                as u32,
                            nand_class: fidl_fuchsia_hardware_nand::Class::Unknown,
                            partition_guid: [0; 16],
                        };
                        responder.send(zx::Status::OK.into_raw(), Some(&mut info)).unwrap();
                    }
                    BrokerRequest::Read { request, responder } => {
                        self.stats.lock().await.reads += 1;
                        let offset = (request.offset_nand * PAGE_SIZE) as usize;
                        let len = (request.length * PAGE_SIZE) as usize;
                        let data = self.data.lock().await;
                        if offset + len > data.len() {
                            responder.send(zx::Status::OUT_OF_RANGE.into_raw(), 0).unwrap();
                            continue;
                        }

                        let slice = &data[offset..offset + len];
                        request.vmo.unwrap().write(slice, request.offset_data_vmo).unwrap();
                        responder.send(zx::Status::OK.into_raw(), 0).unwrap();
                    }
                    BrokerRequest::WriteBytes { request, responder } => {
                        self.stats.lock().await.writes += 1;
                        let mut data = self.data.lock().await;
                        if request.offset_nand + request.length > (data.len() as u64) {
                            responder.send(zx::Status::OUT_OF_RANGE.into_raw()).unwrap();
                            continue;
                        }

                        request
                            .vmo
                            .read(
                                &mut data[(request.offset_nand as usize)
                                    ..(request.offset_nand + request.length) as usize],
                                0,
                            )
                            .unwrap();
                        responder.send(zx::Status::OK.into_raw()).unwrap();
                    }
                    BrokerRequest::Erase { request, responder } => {
                        self.stats.lock().await.erases += 1;
                        let offset = request.offset_nand * PAGES_PER_BLOCK * PAGE_SIZE;
                        let length = request.length * PAGES_PER_BLOCK * PAGE_SIZE;

                        let mut data = self.data.lock().await;
                        if (offset + length) as usize > data.len() {
                            responder.send(zx::Status::OUT_OF_RANGE.into_raw()).unwrap();
                            continue;
                        }

                        data[offset as usize..(offset + length) as usize].fill(0xff);
                        responder.send(zx::Status::OK.into_raw()).unwrap();
                    }
                    _ => {}
                }
            }
            Ok(())
        }

        pub async fn stats(&self) -> Stats {
            *self.stats.lock().await
        }
    }

    #[test]
    fn test_find_flashmap_header() {
        let header = FlashmapHeader::default();

        let mut data_buffer = Vec::new();
        let slice = make_slice(&header);
        data_buffer.resize(4 * slice.len(), 0xab);
        data_buffer[32..32 + slice.len()].clone_from_slice(slice);

        let (result, offset) =
            Flashmap::find_flashmap_header(&data_buffer).expect("Found a header");

        assert_eq!(offset, 32);
        assert_eq!(result, &header);
    }

    #[test]
    fn test_find_flashmap_header_invalid_version() {
        let mut header = FlashmapHeader::default();
        header.ver_minor = 32;
        let mut data_buffer = Vec::new();
        let slice = make_slice(&header);

        data_buffer.resize(4 * slice.len(), 0xab);
        data_buffer[32..32 + slice.len()].clone_from_slice(slice);

        assert_eq!(Flashmap::find_flashmap_header(&data_buffer).is_none(), true);
    }

    #[test]
    fn test_find_flashmap_header_name_not_null_terminated() {
        let mut header = FlashmapHeader::default();
        header.name.copy_from_slice(&[0xab; 32]);

        let mut data_buffer = Vec::new();
        let slice = make_slice(&header);

        data_buffer.resize(4 * slice.len(), 0xab);
        data_buffer[32..32 + slice.len()].clone_from_slice(slice);

        assert_eq!(Flashmap::find_flashmap_header(&data_buffer).is_none(), true);
    }

    #[fuchsia::test]
    async fn test_find_flashmap_in_same_read() {
        let nand = FakeNandDevice::new(4096);
        let mut header = FlashmapHeader::default();
        header.nareas = 2;

        let area = RawFlashmapArea::new("area1", 0, 256);
        let area2 = RawFlashmapArea::new("area2", 256, 1024);

        let mut base = 97;

        {
            let mut data = nand.data.lock().await;
            // Write the header and the areas.
            let slice = make_slice(&header);
            data[base..base + slice.len()].copy_from_slice(slice);
            base += slice.len();
            let slice = make_slice(&area);
            data[base..base + slice.len()].copy_from_slice(slice);
            base += slice.len();
            let slice = make_slice(&area2);
            data[base..base + slice.len()].copy_from_slice(slice);
        }

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();

        let nand = Arc::new(nand);
        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let _flashmap = Flashmap::new(proxy, None).await.expect("Flashmap OK");
        assert!(nand.stats().await.reads == 1);
    }

    #[fuchsia::test]
    async fn test_find_flashmap_split_across_read() {
        let nand = FakeNandDevice::new(zx::system_get_page_size() * 8);
        let mut header = FlashmapHeader::default();
        header.nareas = 2;

        let area = RawFlashmapArea::new("area1", 0, 256);
        let area2 = RawFlashmapArea::new("area2", 256, 1024);

        let mut base =
            (4 * zx::system_get_page_size() as usize) - std::mem::size_of::<FlashmapHeader>();
        base -= 4;

        {
            let mut data = nand.data.lock().await;
            // Write the header and the areas.
            let slice = make_slice(&header);
            data[base..base + slice.len()].copy_from_slice(slice);
            base += slice.len();
            let slice = make_slice(&area);
            data[base..base + slice.len()].copy_from_slice(slice);
            base += slice.len();
            let slice = make_slice(&area2);
            data[base..base + slice.len()].copy_from_slice(slice);
        }

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();
        let nand = Arc::new(nand);
        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let _flashmap = Flashmap::new(proxy, None).await.expect("Flashmap OK");

        assert!(nand.stats().await.reads > 1);
    }

    #[fuchsia::test]
    async fn test_find_flashmap_fails() {
        let nand = Arc::new(FakeNandDevice::new(32));
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();

        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let flashmap = Flashmap::new(proxy, None).await;
        assert_eq!(flashmap.is_err(), true);
    }

    #[fuchsia::test]
    async fn test_flashmap_write() {
        let nand = Arc::new(FakeNandDevice::new_with_flashmap());
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();
        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let data: [u8; 6] = [0, 1, 2, 3, 4, 5];
        let vmo = zx::Vmo::create(data.len() as u64).unwrap();
        vmo.write(&data, 0).expect("vmo write ok");

        let buffer = fidl_fuchsia_mem::Buffer { vmo, size: data.len() as u64 };

        let flashmap = Flashmap::new(proxy, None).await.expect("Flashmap OK");
        flashmap.write_flashmap_area("HELLO", 10, buffer).await.expect("Write succeeds");

        let written_data = nand.data.lock().await;
        let slice = &written_data[10..10 + data.len()];
        assert_eq!(data, slice);
    }

    #[fuchsia::test]
    async fn test_flashmap_erase_invalid_params() {
        let nand = Arc::new(FakeNandDevice::new_with_flashmap());
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();
        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let flashmap = Flashmap::new(proxy, None).await.expect("Flashmap OK");
        flashmap
            .erase_flashmap_area("HELLO", 1, PAGE_SIZE * PAGES_PER_BLOCK)
            .await
            .expect_err("Erase should fail.");

        flashmap
            .erase_flashmap_area("HELLO", 0, PAGE_SIZE * PAGES_PER_BLOCK - 1)
            .await
            .expect_err("Erase should fail.");
    }

    #[fuchsia::test]
    async fn test_flashmap_erase_succeeds() {
        let nand = Arc::new(FakeNandDevice::new_with_flashmap());
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();
        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let flashmap = Flashmap::new(proxy, None).await.expect("Flashmap OK");
        flashmap
            .erase_flashmap_area("HELLO", 0, PAGE_SIZE * PAGES_PER_BLOCK)
            .await
            .expect("Erase should succeed.");

        let data = nand.data.lock().await;
        assert_eq!(
            data[0..(PAGE_SIZE * PAGES_PER_BLOCK) as usize].iter().all(|v| *v == 0xff),
            true
        );
    }

    #[fuchsia::test]
    async fn test_flashmap_with_address_hint() {
        let nand = FakeNandDevice::new(zx::system_get_page_size() * 8);
        let mut header = FlashmapHeader::default();
        header.nareas = 2;

        let area = RawFlashmapArea::new("area1", 0, 256);
        let area2 = RawFlashmapArea::new("area2", 256, 1024);

        let fmap_addr = (4 * zx::system_get_page_size() as usize) + 4;
        let mut base = fmap_addr;

        {
            let mut data = nand.data.lock().await;
            // Write the header and the areas.
            let slice = make_slice(&header);
            data[base..base + slice.len()].copy_from_slice(slice);
            base += slice.len();
            let slice = make_slice(&area);
            data[base..base + slice.len()].copy_from_slice(slice);
            base += slice.len();
            let slice = make_slice(&area2);
            data[base..base + slice.len()].copy_from_slice(slice);
        }

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();
        let nand = Arc::new(nand);
        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let _flashmap = Flashmap::new(proxy, Some(fmap_addr as u64)).await.expect("Flashmap OK");

        assert_eq!(nand.stats().await.reads, 1);
    }
}
