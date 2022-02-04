// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        lsm_tree::types::ItemRef,
        object_handle::{ObjectHandle, ReadObjectHandle},
        object_store::extent_record::{ExtentKey, ExtentValue, DEFAULT_DATA_ATTRIBUTE_ID},
        range::RangeExt,
    },
    anyhow::{anyhow, bail, Error},
    async_trait::async_trait,
    std::{
        cmp::min,
        ops::Range,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
    storage_device::{
        buffer::{Buffer, MutableBufferRef},
        Device,
    },
};

// To read the super-block and journal, we use a special reader since we cannot use the object-store
// reader until we've replayed the whole journal.  Clients must supply the extents to be used.
pub struct Handle {
    object_id: u64,
    device: Arc<dyn Device>,
    start_offset: u64,
    // A list of extents we know of for the handle.  The extents are all logically contiguous and
    // start from |start_offset|, so we don't bother storing the logical offsets.
    extents: Vec<Range<u64>>,
    size: u64,
    trace: AtomicBool,
}

impl Handle {
    pub fn new(object_id: u64, device: Arc<dyn Device>) -> Self {
        Self {
            object_id,
            device,
            start_offset: 0,
            extents: Vec::new(),
            size: 0,
            trace: AtomicBool::new(false),
        }
    }

    pub fn push_extent(&mut self, r: Range<u64>) {
        self.size += r.length().unwrap();
        self.extents.push(r);
    }

    pub fn try_push_extent(
        &mut self,
        item: ItemRef<'_, ExtentKey, ExtentValue>,
    ) -> Result<bool, Error> {
        match item {
            ItemRef {
                key: ExtentKey { object_id, attribute_id: DEFAULT_DATA_ATTRIBUTE_ID, range },
                value: ExtentValue::Some { device_offset, .. },
                ..
            } if *object_id == self.object_id => {
                if self.extents.is_empty() {
                    self.start_offset = range.start;
                } else if range.start != self.start_offset + self.size {
                    bail!(anyhow!(FxfsError::Inconsistent).context(format!(
                        "Unexpected journal extent {:?}, expected start: {}",
                        item, self.size
                    )));
                }
                self.push_extent(*device_offset..*device_offset + range.length()?);
                Ok(true)
            }
            _ => Ok(false),
        }
    }

    // Discard any extents whose logical offset succeeds |offset|.
    pub fn discard_extents(&mut self, discard_offset: u64) {
        let mut offset = self.start_offset + self.size;
        let mut num = 0;
        while let Some(extent) = self.extents.last() {
            let length = extent.length().unwrap();
            offset = offset.checked_sub(length).unwrap();
            if offset < discard_offset {
                break;
            }
            self.size -= length;
            self.extents.pop();
            num += 1;
        }
        if self.trace.load(Ordering::Relaxed) {
            log::info!("JH: Discarded {} extents from offset {}", num, discard_offset);
        }
    }
}

// TODO(csuter): This doesn't need to be ObjectHandle any more and we could integrate this into
// JournalReader.
impl ObjectHandle for Handle {
    fn object_id(&self) -> u64 {
        self.object_id
    }

    fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        self.device.allocate_buffer(size)
    }

    fn block_size(&self) -> u64 {
        self.device.block_size().into()
    }

    fn get_size(&self) -> u64 {
        self.size
    }
    fn set_trace(&self, trace: bool) {
        let old_value = self.trace.swap(trace, Ordering::Relaxed);
        if trace != old_value {
            log::info!(
                "JH {} tracing {}",
                self.object_id,
                if trace { "enabled" } else { "disabled" },
            );
        }
    }
}

#[async_trait]
impl ReadObjectHandle for Handle {
    async fn read(&self, mut offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
        assert!(offset >= self.start_offset);
        let trace = self.trace.load(Ordering::Relaxed);
        if trace {
            log::info!("JH: read {}@{}", buf.len(), offset);
        }
        let len = buf.len();
        let mut buf_offset = 0;
        let mut file_offset = self.start_offset;
        for extent in &self.extents {
            let extent_len = extent.end - extent.start;
            if offset < file_offset + extent_len {
                if trace {
                    log::info!("JH: matching extent {:?}", extent);
                }
                let device_offset = extent.start + offset - file_offset;
                let to_read = min(extent.end - device_offset, (len - buf_offset) as u64) as usize;
                assert!(buf_offset % self.device.block_size() as usize == 0);
                self.device
                    .read(
                        device_offset,
                        buf.reborrow().subslice_mut(buf_offset..buf_offset + to_read),
                    )
                    .await?;
                buf_offset += to_read;
                if buf_offset == len {
                    break;
                }
                offset += to_read as u64;
            }
            file_offset += extent_len;
        }
        Ok(len)
    }
}
