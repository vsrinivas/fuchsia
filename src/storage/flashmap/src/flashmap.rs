// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util::MappedVmo,
    anyhow::{anyhow, Context, Error},
    fidl::HandleBased,
    fidl_fuchsia_hardware_nand::Info as NandInfo,
    fidl_fuchsia_nand::{BrokerProxy, BrokerRequestData},
    fidl_fuchsia_nand_flashmap::{FlashmapRequest, FlashmapRequestStream},
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, TryStreamExt},
    std::{collections::HashMap, convert::TryFrom, ffi::CStr},
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
    pub async fn new(device: BrokerProxy) -> Result<Self, Error> {
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

        map.find_flashmap().await?;

        Ok(map)
    }

    /// Perform a linear search on the NAND device to find the flashmap header.
    // TODO(simonshields): use the coreboot table as a hint so we don't have to search blindly.
    async fn find_flashmap(&mut self) -> Result<(), Error> {
        let max_offset = (self.info.pages_per_block as u32) * (self.info.num_blocks as u32);
        if self.vmo_size % (self.info.page_size as usize) != 0 {
            return Err(anyhow!("Page size must divide evenly into VMO size."));
        }

        let inner = self.inner.get_mut();

        let pages_per_vmo = self.vmo_size / (self.info.page_size as usize);

        let data = inner.mapping.as_slice();

        let mut request = BrokerRequestData {
            vmo: None,
            length: pages_per_vmo as u32,
            data_vmo: true,
            offset_data_vmo: 0,
            offset_oob_vmo: 0,
            offset_nand: 0,
            oob_vmo: false,
        };

        let mut read_start_page = 0;

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
                _ => {}
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
    }

    const PAGE_SIZE: u32 = 32;
    const PAGES_PER_BLOCK: u32 = 4;
    pub struct FakeNandDevice {
        data: Vec<u8>,
        stats: Mutex<Stats>,
    }

    impl FakeNandDevice {
        pub fn new(blocks: u32) -> Self {
            let mut data: Vec<u8> = Vec::new();
            data.resize((blocks * PAGES_PER_BLOCK * PAGE_SIZE) as usize, 0xff);

            FakeNandDevice { data, stats: Mutex::new(Default::default()) }
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

            FakeNandDevice { data, stats: Mutex::new(Default::default()) }
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
                            num_blocks: (self.data.len() / ((PAGE_SIZE * PAGES_PER_BLOCK) as usize))
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
                        if offset + len > self.data.len() {
                            responder.send(zx::Status::OUT_OF_RANGE.into_raw(), 0).unwrap();
                            continue;
                        }

                        let slice = &self.data[offset..offset + len];
                        request.vmo.unwrap().write(slice, request.offset_data_vmo).unwrap();
                        responder.send(zx::Status::OK.into_raw(), 0).unwrap();
                    }
                    _ => todo!(),
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
        let mut nand = FakeNandDevice::new(4096);
        let mut header = FlashmapHeader::default();
        header.nareas = 2;

        let area = RawFlashmapArea::new("area1", 0, 256);
        let area2 = RawFlashmapArea::new("area2", 256, 1024);

        let mut base = 97;

        // Write the header and the areas.
        let slice = make_slice(&header);
        nand.data[base..base + slice.len()].copy_from_slice(slice);
        base += slice.len();
        let slice = make_slice(&area);
        nand.data[base..base + slice.len()].copy_from_slice(slice);
        base += slice.len();
        let slice = make_slice(&area2);
        nand.data[base..base + slice.len()].copy_from_slice(slice);

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();

        let nand = Arc::new(nand);
        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let _flashmap = Flashmap::new(proxy).await.expect("Flashmap OK");
        assert!(nand.stats().await.reads == 1);
    }

    #[fuchsia::test]
    async fn test_find_flashmap_split_across_read() {
        let mut nand = FakeNandDevice::new(zx::system_get_page_size() * 8);
        let mut header = FlashmapHeader::default();
        header.nareas = 2;

        let area = RawFlashmapArea::new("area1", 0, 256);
        let area2 = RawFlashmapArea::new("area2", 256, 1024);

        let mut base =
            (4 * zx::system_get_page_size() as usize) - std::mem::size_of::<FlashmapHeader>();
        base -= 4;

        // Write the header and the areas.
        let slice = make_slice(&header);
        nand.data[base..base + slice.len()].copy_from_slice(slice);
        base += slice.len();
        let slice = make_slice(&area);
        nand.data[base..base + slice.len()].copy_from_slice(slice);
        base += slice.len();
        let slice = make_slice(&area2);
        nand.data[base..base + slice.len()].copy_from_slice(slice);

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<BrokerMarker>().unwrap();
        let nand = Arc::new(nand);
        let clone = Arc::clone(&nand);
        Task::spawn(async move {
            clone.serve(stream).await.unwrap();
        })
        .detach();

        let _flashmap = Flashmap::new(proxy).await.expect("Flashmap OK");

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

        let flashmap = Flashmap::new(proxy).await;
        assert_eq!(flashmap.is_err(), true);
    }
}
