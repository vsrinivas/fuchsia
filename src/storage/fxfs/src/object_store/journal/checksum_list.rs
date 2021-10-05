// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::journal::fletcher64,
    anyhow::Error,
    interval_tree::utils::RangeOps,
    std::{
        collections::{btree_map::Entry, BTreeMap},
        ops::Range,
    },
    storage_device::Device,
};

#[derive(Debug)]
struct ChecksumEntry {
    journal_offset: u64,
    device_range: Range<u64>,
    checksums: Vec<(/* checksum */ u64, /* journal offset dependency */ u64)>,
}

#[derive(Default)]
pub struct ChecksumList {
    // The offset that is known to have been flushed to the device.  Any entries in the journal that
    // are prior to this point are ignored since there is no need to verify those checksums.
    flushed_offset: u64,

    // This is a list of checksums that we might need to verify, in journal order.
    checksum_entries: Vec<ChecksumEntry>,

    // Records a mapping from the starting offset of the device range, to the entry index.
    device_offset_to_checksum_entry: BTreeMap<u64, usize>,

    // The maximum chunk size within checksum_entries which determines the size of the buffer we
    // need to allocate during verification.
    max_chunk_size: usize,
}

impl ChecksumList {
    pub fn new(flushed_offset: u64) -> Self {
        ChecksumList { flushed_offset, ..Default::default() }
    }

    /// Adds an extent that might need its checksum verifying.  Extents must be pushed in
    /// journal-offset order.
    pub fn push(&mut self, journal_offset: u64, device_range: Range<u64>, checksums: &Vec<u64>) {
        if journal_offset < self.flushed_offset {
            // Ignore anything that was prior to being flushed.
            return;
        }
        // This can be changed to try_insert when available.
        // If this is a duplicate, we don't need to verify the checksum twice, and we want to
        // keep the first entry because it comes earlier in the journal.
        // TODO(csuter): If this is a duplicate, we should check that the checksums match.
        if let Entry::Vacant(v) = self.device_offset_to_checksum_entry.entry(device_range.end) {
            v.insert(self.checksum_entries.len());
        }
        let chunk_size = (device_range.length() / checksums.len() as u64) as usize;
        if chunk_size > self.max_chunk_size {
            self.max_chunk_size = chunk_size;
        }
        let mut entry = ChecksumEntry {
            journal_offset,
            device_range,
            checksums: Vec::with_capacity(checksums.len()),
        };
        for c in checksums {
            entry.checksums.push((*c, u64::MAX - 1));
        }
        self.checksum_entries.push(entry);
    }

    /// Marks an extent as deallocated.  If this journal-offset ends up being replayed, it means
    /// that we can skip a previously queued checksum.
    pub fn mark_deallocated(&mut self, journal_offset: u64, mut device_range: Range<u64>) {
        if journal_offset < self.flushed_offset {
            // Ignore anything that was prior to being flushed.
            return;
        }
        let mut r = self.device_offset_to_checksum_entry.range(device_range.start + 1..);
        while let Some((_, index)) = r.next() {
            let entry = &mut self.checksum_entries[*index];
            if entry.device_range.start >= device_range.end {
                break;
            }
            let chunk_size = (entry.device_range.length() / entry.checksums.len() as u64) as usize;
            let checksum_index_start = if device_range.start < entry.device_range.start {
                0
            } else {
                (device_range.start - entry.device_range.start) as usize / chunk_size
            };
            // Figure out the overlap.
            if entry.device_range.end >= device_range.end {
                // TODO: check that chunk size is aligned.
                let checksum_index_end =
                    (device_range.end - entry.device_range.start) as usize / chunk_size;
                // This entry covers the remainder.
                entry.checksums[checksum_index_start..checksum_index_end]
                    .iter_mut()
                    .for_each(|c| c.1 = journal_offset);
                break;
            }
            entry.checksums[checksum_index_start..].iter_mut().for_each(|c| c.1 = journal_offset);
            device_range.start = entry.device_range.end;
        }
    }

    /// Verifies the checksums in the list.  `journal_offset` should indicate the last journal
    /// offset read and verify will return the journal offset that it is safe to replay up to.
    /// `flushed_offset` indicates the offset that we know to have been flushed and so we don't need
    /// to perform verification.
    pub async fn verify(&self, device: &dyn Device, mut journal_offset: u64) -> Result<u64, Error> {
        let mut last_journal_offset = u64::MAX;
        let mut buf = device.allocate_buffer(self.max_chunk_size);
        'try_again: loop {
            for e in &self.checksum_entries {
                if e.journal_offset >= journal_offset {
                    break;
                }
                let chunk_size = (e.device_range.length() / e.checksums.len() as u64) as usize;
                let mut offset = e.device_range.start;
                for (checksum, dependency) in e.checksums.iter() {
                    // We only need to verify the checksum if we know the dependency isn't going to
                    // be replayed and we can skip verifications that we know were done on the
                    // previous iteration of the loop.
                    if *dependency >= journal_offset && *dependency < last_journal_offset {
                        device.read(offset, buf.subslice_mut(0..chunk_size)).await?;
                        if fletcher64(&buf.as_slice()[0..chunk_size], 0) != *checksum {
                            // Verification failed, so we need to reset the journal_offset to this
                            // entry and try again.
                            last_journal_offset = journal_offset;
                            journal_offset = e.journal_offset;
                            continue 'try_again;
                        }
                    }
                    offset += chunk_size as u64;
                }
            }
            return Ok(journal_offset);
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::ChecksumList,
        crate::object_store::journal::fletcher64,
        fuchsia_async as fasync,
        storage_device::{fake_device::FakeDevice, Device},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_verify() {
        let device = FakeDevice::new(2048, 512);
        let mut buffer = device.allocate_buffer(2048);
        let mut list = ChecksumList::new(0);

        buffer.as_mut_slice()[0..512].copy_from_slice(&[1; 512]);
        buffer.as_mut_slice()[512..1024].copy_from_slice(&[2; 512]);
        buffer.as_mut_slice()[1024..1536].copy_from_slice(&[3; 512]);
        buffer.as_mut_slice()[1536..2048].copy_from_slice(&[4; 512]);
        device.write(512, buffer.as_ref()).await.expect("write failed");
        list.push(
            1,
            512..2048,
            &vec![fletcher64(&[1; 512], 0), fletcher64(&[2; 512], 0), fletcher64(&[3; 512], 0)],
        );

        // All entries should pass.
        assert_eq!(list.verify(&device, 10).await.expect("verify failed"), 10);

        // Corrupt the middle of the three 512 byte blocks.
        buffer.as_mut_slice()[512] = 0;
        device.write(512, buffer.as_ref()).await.expect("write failed");

        // Verification should fail now.
        assert_eq!(list.verify(&device, 10).await.expect("verify failed"), 1);

        // Mark the middle block as deallocated and then it should pass again.
        list.mark_deallocated(2, 1024..1536);
        assert_eq!(list.verify(&device, 10).await.expect("verify failed"), 10);

        // Add another entry followed by a deallocation.
        list.push(3, 2048..2560, &vec![fletcher64(&[4; 512], 0)]);
        list.mark_deallocated(4, 1536..2048);

        // All entries should validate.
        assert_eq!(list.verify(&device, 10).await.expect("verify failed"), 10);

        // Now corrupt the block at 2048.
        buffer.as_mut_slice()[1536] = 0;
        device.write(512, buffer.as_ref()).await.expect("write failed");

        // This should only validate up to journal offset 3.
        assert_eq!(list.verify(&device, 10).await.expect("verify failed"), 3);

        // Corrupt the block that was marked as deallocated in #4.
        buffer.as_mut_slice()[1024] = 0;
        device.write(512, buffer.as_ref()).await.expect("write failed");

        // The deallocation in #4 should be ignored and so validation should only succeed up
        // to offset 1.
        assert_eq!(list.verify(&device, 10).await.expect("verify failed"), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_verify_entry_prior_to_flushed_offset_is_ignored() {
        let device = FakeDevice::new(2048, 512);
        let mut buffer = device.allocate_buffer(2048);
        let mut list = ChecksumList::new(2);

        buffer.as_mut_slice()[0..512].copy_from_slice(&[1; 512]);
        buffer.as_mut_slice()[512..1024].copy_from_slice(&[2; 512]);
        device.write(512, buffer.as_ref()).await.expect("write failed");

        // This entry has the wrong checksum will fail, but it should be ignored anyway because it
        // is prior to the flushed offset.
        list.push(1, 512..1024, &vec![fletcher64(&[2; 512], 0)]);

        list.push(2, 1024..1536, &vec![fletcher64(&[2; 512], 0)]);

        assert_eq!(list.verify(&device, 10).await.expect("verify failed"), 10);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_deallocate_overlap() {
        let device = FakeDevice::new(2048, 512);
        let mut buffer = device.allocate_buffer(512);
        let mut list = ChecksumList::new(1);

        buffer.as_mut_slice().copy_from_slice(&[2; 512]);
        device.write(2560, buffer.as_ref()).await.expect("write failed");

        list.push(2, 512..1024, &vec![fletcher64(&[1; 512], 0)]);
        list.mark_deallocated(3, 0..1024);
        list.push(4, 2048..3072, &vec![fletcher64(&[2; 512], 0); 2]);
        list.mark_deallocated(5, 1536..2560);

        assert_eq!(list.verify(&device, 10).await.expect("verify failed"), 10);
    }
}
