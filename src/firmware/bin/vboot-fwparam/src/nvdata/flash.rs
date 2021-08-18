// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Flash storage support for nvdata.
//! On x86 platforms, the flash nvdata is used as a backup of the CMOS copy.
//! On ARM, nvdata is either stored on flash or on disk.

use anyhow::{anyhow, Context, Error};
use fidl_fuchsia_nand_flashmap::{Area, FlashmapProxy};
use fuchsia_zircon as zx;
use std::convert::TryInto;

const NVRAM_AREA_NAME: &str = "RW_NVRAM";

pub struct Flash {
    proxy: FlashmapProxy,
    area: Area,
}

impl Flash {
    pub async fn new_with_proxy(proxy: FlashmapProxy) -> Result<Flash, Error> {
        let areas = proxy.get_areas().await.context("Getting areas")?;

        let area = match areas.iter().find(|&a| a.name == NVRAM_AREA_NAME) {
            Some(a) => a.clone(),
            None => return Err(anyhow::anyhow!("No area?")),
        };

        Ok(Flash { proxy, area })
    }

    /// Finds the last non-blank (0xff) region of |size| bytes in |buf| and returns its index (where
    /// index * |size| == byte offset in |buf|).
    fn find_cur_vbnv_index(&self, buf: &[u8], size: usize) -> Option<usize> {
        if buf.len() % size != 0 {
            panic!("RW_NVRAM length must be a multiple of nvram size.");
        }

        // This finds the first blank region.
        match buf
            .chunks(size)
            .enumerate()
            .find(|(_, chunk)| chunk.iter().all(|b| *b == 0xff))
            .map(|(index, _)| index)
        {
            // If the first region was blank, that means there are no non-blank regions.
            Some(0) => None,
            // Any other region means that the region before it was non-blank.
            Some(n) => Some(n - 1),
            // If we found no blank regions, then return the last region.
            None => Some((buf.len() / size) - 1),
        }
    }

    pub async fn save(&self, nvram: &[u8]) -> Result<(), Error> {
        let result = self
            .proxy
            .read(&self.area.name, 0, self.area.size)
            .await
            .context("Sending FIDL read")?
            .map_err(zx::Status::from_raw)
            .context("Reading RW_NVRAM")?;

        let mut data = Vec::with_capacity(result.size as usize);
        data.resize(result.size as usize, 0);
        result.vmo.read(&mut data.as_mut_slice(), result.offset).context("Reading from VMO")?;

        let cur_slot = self
            .find_cur_vbnv_index(data.as_slice(), nvram.len())
            .ok_or(anyhow!("Firmware should write at least one nvram entry"))?;

        let mut next_entry_start = (cur_slot + 1) * nvram.len();
        if next_entry_start == self.area.size as usize {
            // Clear the NVRAM region and start again.
            next_entry_start = 0;
            self.proxy
                .erase(&self.area.name, 0, self.area.size)
                .await
                .context("Sending erase")?
                .map_err(zx::Status::from_raw)
                .context("Erasing RW_NVRAM")?;
        }

        // Write the new data. We reuse the vmo we got sent.
        result.vmo.write(nvram, 0).context("Writing new nvram to vmo")?;
        let mut buffer = fidl_fuchsia_mem::Buffer { vmo: result.vmo, size: nvram.len() as u64 };
        self.proxy
            .write(
                &self.area.name,
                next_entry_start.try_into().context("next_entry_start is out of bounds")?,
                &mut buffer,
            )
            .await
            .context("Sending FIDL write")?
            .map_err(zx::Status::from_raw)
            .context("Writing RW_NVRAM")?;

        Ok(())
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use fidl_fuchsia_nand_flashmap::{AreaFlags, FlashmapMarker, FlashmapRequest};
    use futures::{lock::Mutex, TryStreamExt};
    use std::sync::Arc;

    pub struct FakeFlash {
        pub data: Mutex<Vec<u8>>,
    }

    const NVRAM_REGION_SIZE: u32 = 4096;

    impl FakeFlash {
        pub fn new(nvram_size: usize) -> Arc<Self> {
            let mut data = Vec::new();
            data.resize(NVRAM_REGION_SIZE as usize, 0xff);
            data[0..nvram_size].fill(0);
            Arc::new(FakeFlash { data: Mutex::new(data) })
        }

        pub fn get_proxy(self: Arc<Self>) -> FlashmapProxy {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<FlashmapMarker>().unwrap();

            fuchsia_async::Task::spawn(async move {
                while let Some(req) = stream.try_next().await.unwrap() {
                    match req {
                        FlashmapRequest::GetAreas { responder } => {
                            let areas: &mut [Area] = &mut [Area {
                                name: NVRAM_AREA_NAME.to_owned(),
                                size: NVRAM_REGION_SIZE,
                                offset: 0,
                                flags: AreaFlags::empty(),
                            }];
                            responder.send(&mut areas.iter_mut()).expect("Reply OK");
                        }
                        FlashmapRequest::GetEraseBlockSize { .. } => todo!(),
                        FlashmapRequest::Read { name, offset, size, responder } => {
                            assert_eq!(&name, NVRAM_AREA_NAME);
                            assert_eq!(offset, 0);
                            assert_eq!(size, NVRAM_REGION_SIZE);
                            let vmo = zx::Vmo::create(size as u64).unwrap();
                            let data = self.data.lock().await;
                            vmo.write(data.as_slice(), 0).unwrap();
                            let range =
                                fidl_fuchsia_mem::Range { vmo, size: size as u64, offset: 0 };
                            responder.send(&mut Ok(range)).unwrap();
                        }
                        FlashmapRequest::Write { name, offset, data: to_write, responder } => {
                            assert_eq!(&name, NVRAM_AREA_NAME);
                            let mut buf = Vec::new();
                            buf.resize(to_write.size as usize, 0);
                            to_write.vmo.read(buf.as_mut_slice(), 0).unwrap();

                            let offset: usize = offset as usize;
                            let mut data = self.data.lock().await;
                            assert_eq!(
                                data[offset..offset + buf.len()].iter().all(|v| *v == 0xff),
                                true,
                                "Writing a non-erased area"
                            );
                            data[offset..offset + buf.len()].copy_from_slice(&buf);
                            responder.send(&mut Ok(())).unwrap();
                        }
                        FlashmapRequest::Erase { name, offset, range, responder } => {
                            assert_eq!(&name, NVRAM_AREA_NAME);
                            assert_eq!(offset, 0);
                            assert_eq!(range, NVRAM_REGION_SIZE);
                            self.data.lock().await.fill(0xff);
                            responder.send(&mut Ok(())).unwrap();
                        }
                    }
                }
            })
            .detach();

            proxy
        }

        /// Make default flash for tests that don't care about its contents.
        pub async fn empty() -> Flash {
            let flash = FakeFlash::new(NVDATA_SIZE);
            let proxy = flash.get_proxy();
            Flash::new_with_proxy(proxy).await.expect("Found area OK")
        }

        pub async fn get_flash(self: Arc<Self>) -> Flash {
            let proxy = self.get_proxy();
            Flash::new_with_proxy(proxy).await.expect("Found area OK")
        }
    }

    const NVDATA_SIZE: usize = 16;
    #[fuchsia::test]
    async fn test_flash_connect() {
        let flash = FakeFlash::new(NVDATA_SIZE);

        let proxy = flash.get_proxy();

        let _ = Flash::new_with_proxy(proxy).await.expect("Found area OK");
    }

    #[fuchsia::test]
    async fn test_flash_save() {
        let fake = FakeFlash::new(NVDATA_SIZE);
        let proxy = Arc::clone(&fake).get_proxy();
        let flash = Flash::new_with_proxy(proxy).await.unwrap();

        let data = [0xab; NVDATA_SIZE];
        flash.save(&data).await.expect("Save OK");
        assert_eq!(fake.data.lock().await[NVDATA_SIZE..NVDATA_SIZE * 2], data);
    }

    #[fuchsia::test]
    async fn test_flash_wraparound() {
        let fake = FakeFlash::new(NVDATA_SIZE);
        {
            fake.data.lock().await.fill(0xab);
        }
        let proxy = Arc::clone(&fake).get_proxy();
        let flash = Flash::new_with_proxy(proxy).await.unwrap();

        let data = [0xcd; NVDATA_SIZE];
        flash.save(&data).await.expect("Save OK");
        assert_eq!(fake.data.lock().await[0..NVDATA_SIZE], data);
    }
}
