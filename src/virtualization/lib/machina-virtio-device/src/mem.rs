// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::{self as zx},
    virtio_device::{
        mem::{DeviceRange, DriverMem, DriverRange},
        queue::QueueMemory,
        ring,
    },
};

/// Provide access to guest memory.
///
/// Takes a [`zx::Vmo`] that represents guest memory and provides an implementation of
/// [`DriverMem`].
// # Safety
// If mapping is not none then it must be the address of mapping in the root vmar, such that it may
// be unmapped according to drop. Once a mapping is set to Some it must not be changed until the
// object is dropped.
pub struct GuestMem {
    mapping: Option<(usize, usize)>,
}

impl Drop for GuestMem {
    fn drop(&mut self) {
        if let Some((base, len)) = self.mapping.take() {
            // If a mapping was set we know it is from a vmar::map and can unmap it.
            unsafe { fuchsia_runtime::vmar_root_self().unmap(base, len) }.unwrap();
        }
    }
}

impl GuestMem {
    /// Construct a new, empty, [`GuestMem`].
    ///
    /// Before a [`GuestMem`] can be used to perform [`translations`](DriverMem) it needs to have
    /// its mappings populated through [`give_vmo`](#give_vmo).
    pub fn new() -> GuestMem {
        // Initially no mapping.
        GuestMem { mapping: None }
    }

    /// Initialize a [`GuestMem`] with a [`zx::Vmo`]
    ///
    /// This takes a reference to the [`zx::Vmo`] provided in the `StartInfo` message to devices.
    pub fn set_vmo(&mut self, vmo: &zx::Vmo) -> Result<(), zx::Status> {
        if self.mapping.is_some() {
            return Err(zx::Status::BAD_STATE);
        }
        let vmo_size = vmo.get_size()? as usize;
        let addr = fuchsia_runtime::vmar_root_self().map(
            0,
            vmo,
            0,
            vmo_size,
            zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
        )?;
        self.mapping = Some((addr, vmo_size));
        Ok(())
    }

    pub fn get_mapping(&self) -> Option<(usize, usize)> {
        self.mapping
    }
}

impl DriverMem for GuestMem {
    fn translate<'a>(&'a self, driver: DriverRange) -> Option<DeviceRange<'a>> {
        self.mapping.as_ref().and_then(|&(base, len)| {
            // If we found a mapping then we know this DeviceRange construction is safe since
            // - The range is mapped to a valid VMO and has not been unmapped, as that can only on
            //   drop.
            // - We mapped this range and therefore know that it cannot alias any other rust objects
            // - We will not unmap this range until drop, hence the borrow and lifetime on this
            //   function ensure it remains valid.
            let mem = unsafe { DeviceRange::new(base..(base + len)) };
            // From all the memory attempt to split out the range requested.
            Some(mem.split_at(driver.0.start)?.1.split_at(driver.len())?.0)
        })
    }
}

/// Construct a new [`GuestMem`] containing the given [`zx::Vmo`]
///
/// See [`GuestMem::give_vmo`] for details on how the [`zx::Vmo`] handle is used.
pub fn guest_mem_from_vmo(vmo: &zx::Vmo) -> Result<GuestMem, zx::Status> {
    let mut mem = GuestMem::new();
    mem.set_vmo(vmo)?;
    Ok(mem)
}

/// Helper for constructing a [`QueueMemory`]
///
/// Takes a queue description, similar to the form of a [`ConfigureQueue`]
/// (fidl_fuchsia_virtualization_hardware::VirtioDeviceRequest::ConfigureQueue), and attempts to
/// [`translate`] each of the ranges to build a [`QueueMemory`].
pub fn translate_queue<'a, M: DriverMem>(
    mem: &'a M,
    size: u16,
    desc: usize,
    avail: usize,
    used: usize,
) -> Option<QueueMemory<'a>> {
    let desc_len = std::mem::size_of::<ring::Desc>() * size as usize;
    let avail_len = ring::Driver::avail_len_for_queue_size(size as u16);
    let used_len = ring::Device::used_len_for_queue_size(size as u16);
    let desc = mem.translate((desc..desc.checked_add(desc_len)?).into())?;
    let avail = mem.translate((avail..avail.checked_add(avail_len)?).into())?;
    let used = mem.translate((used..used.checked_add(used_len)?).into())?;
    Some(QueueMemory { desc, avail, used })
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_zircon as zx;
    #[test]
    fn test_translate() -> Result<(), anyhow::Error> {
        let size = zx::system_get_page_size() as usize * 1024;
        let vmo = zx::Vmo::create(size as u64)?;
        let koid = vmo.info()?.koid;
        let base;

        let get_maps = || {
            let process = fuchsia_runtime::process_self();
            let (returned, remaining) = process.info_maps(&mut [] as &mut [zx::ProcessMapsInfo])?;
            assert_eq!(returned, 0);
            // Add some extra since our Vec allocation will itself create an additional mapping.
            let count = remaining + 10;
            let mut info = Vec::with_capacity(count);
            info.resize(count, zx::ProcessMapsInfo::default());
            let (returned, remaining) = process.info_maps(info.as_mut_slice())?;
            assert_eq!(remaining, 0);
            info.resize(returned, zx::ProcessMapsInfo::default());
            Result::<_, zx::Status>::Ok(info)
        };

        {
            let mem = guest_mem_from_vmo(&vmo)?;
            // Translate an address so we can get the base.
            base = mem.translate(DriverRange(0..1)).unwrap().get().start;
            // Validate that there is a mapping of the right size.
            assert!(get_maps()?
                .into_iter()
                .find(|info| if let Some(map) = info.into_mapping_info() {
                    map.vmo_koid == koid.raw_koid() && info.base == base && info.size >= size
                } else {
                    false
                })
                .is_some());

            // Check that we can translate some valid ranges.
            assert_eq!(mem.translate(DriverRange(0..64)).unwrap().get(), base..(base + 64));
            assert_eq!(
                mem.translate(DriverRange(size - 64..size)).unwrap().get(),
                (base + size - 64)..(base + size)
            );
            assert_eq!(mem.translate(DriverRange(49..80)).unwrap().get(), (base + 49)..(base + 80));
            assert_eq!(mem.translate(DriverRange(0..size)).unwrap().get(), base..(base + size));

            // Make sure no invalid ranges translate.
            assert_eq!(mem.translate(DriverRange(0..(size + 1))), None);
            assert_eq!(mem.translate(DriverRange((size - 64)..(size + 1))), None);
            assert_eq!(mem.translate(DriverRange((size + 100)..(size + 200))), None);
        }
        // validate that the mapping has gone away and that our VMO is not mapped anywhere at all.
        assert!(get_maps()?
            .into_iter()
            .filter_map(|x| x.into_mapping_info())
            .find(|map| map.vmo_koid == koid.raw_koid())
            .is_none());

        Ok(())
    }
}
