// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon pager objects.

use {
    crate::{ok, AsHandleRef, Handle, HandleBased, HandleRef, Port, Status, Vmo, VmoOptions},
    bitflags::bitflags,
    fuchsia_zircon_sys as sys,
};

/// An object representing a Zircon
/// [pager](https://fuchsia.dev/fuchsia-src/concepts/objects/pager.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Pager(Handle);
impl_handle_based!(Pager);

bitflags! {
    /// Options that may be used when creating a pager.
    #[repr(transparent)]
    pub struct PagerOptions: u32 {
        const _RESERVED = 0; // Placeholder until we add some options.
    }
}

pub enum PagerOp {
    Fail(Status),
    Dirty,
    WritebackBegin,
    WritebackEnd,
}

impl Pager {
    /// See [zx_pager_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_create)
    pub fn create(options: PagerOptions) -> Result<Pager, Status> {
        let mut out = 0;
        let status = unsafe { sys::zx_pager_create(options.bits(), &mut out) };
        ok(status)?;
        Ok(Pager::from(unsafe { Handle::from_raw(out) }))
    }

    /// See [zx_pager_create_vmo](https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_create_vmo)
    pub fn create_vmo(
        &self,
        options: VmoOptions,
        port: &Port,
        key: u64,
        size: u64,
    ) -> Result<Vmo, Status> {
        let mut out = 0;
        let status = unsafe {
            sys::zx_pager_create_vmo(
                self.raw_handle(),
                options.bits(),
                port.raw_handle(),
                key,
                size,
                &mut out,
            )
        };
        ok(status)?;
        Ok(Vmo::from(unsafe { Handle::from_raw(out) }))
    }

    /// See [zx_pager_detach_vmo](https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_detach_vmo)
    pub fn detach_vmo(&self, vmo: &Vmo) -> Result<(), Status> {
        let status = unsafe { sys::zx_pager_detach_vmo(self.raw_handle(), vmo.raw_handle()) };
        ok(status)
    }

    /// See [zx_pager_supply_pages](https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_supply_pages)
    pub fn supply_pages(
        &self,
        vmo: &Vmo,
        range: std::ops::Range<u64>,
        aux_vmo: &Vmo,
        aux_offset: u64,
    ) -> Result<(), Status> {
        let status = unsafe {
            sys::zx_pager_supply_pages(
                self.raw_handle(),
                vmo.raw_handle(),
                range.start,
                range.end - range.start,
                aux_vmo.raw_handle(),
                aux_offset,
            )
        };
        ok(status)
    }

    /// See [zx_pager_op_range](https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_op_range)
    pub fn op_range(
        &self,
        op: PagerOp,
        pager_vmo: &Vmo,
        range: std::ops::Range<u64>,
    ) -> Result<(), Status> {
        let (op, data) = match op {
            PagerOp::Fail(status) => (sys::ZX_PAGER_OP_FAIL, status.into_raw() as u64),
            PagerOp::Dirty => (sys::ZX_PAGER_OP_DIRTY, 0),
            PagerOp::WritebackBegin => (sys::ZX_PAGER_OP_WRITEBACK_BEGIN, 0),
            PagerOp::WritebackEnd => (sys::ZX_PAGER_OP_WRITEBACK_END, 0),
        };
        let status = unsafe {
            sys::zx_pager_op_range(
                self.raw_handle(),
                op,
                pager_vmo.raw_handle(),
                range.start,
                range.end - range.start,
                data,
            )
        };
        ok(status)
    }
}

#[cfg(test)]
mod tests {
    use {crate as zx, std::sync::Arc};

    const KEY: u64 = 5;

    #[test]
    fn create_vmo() {
        let port = zx::Port::create().unwrap();
        let pager = zx::Pager::create(zx::PagerOptions::empty()).unwrap();
        let vmo = pager.create_vmo(zx::VmoOptions::RESIZABLE, &port, KEY, /*size=*/ 100).unwrap();
        let vmo_info = vmo.info().unwrap();
        assert!(vmo_info.flags.contains(zx::VmoInfoFlags::PAGER_BACKED));
        assert!(vmo_info.flags.contains(zx::VmoInfoFlags::RESIZABLE));
    }

    #[test]
    fn detach_vmo() {
        let port = zx::Port::create().unwrap();
        let pager = zx::Pager::create(zx::PagerOptions::empty()).unwrap();
        let vmo = pager.create_vmo(zx::VmoOptions::empty(), &port, KEY, /*size=*/ 100).unwrap();
        pager.detach_vmo(&vmo).unwrap();

        // If the vmo was not detached then the test will time out waiting for the page to be
        // supplied.
        let mut data = [0u8; 100];
        let e = vmo.read(&mut data, 0).expect_err("A detached vmo should fail reads");
        assert_eq!(e, zx::Status::BAD_STATE);
    }

    #[test]
    fn supply_pages() {
        let page_size: u64 = zx::system_get_page_size().into();
        let port = zx::Port::create().unwrap();
        let pager = zx::Pager::create(zx::PagerOptions::empty()).unwrap();
        let vmo =
            Arc::new(pager.create_vmo(zx::VmoOptions::empty(), &port, KEY, page_size).unwrap());

        let read_thread = std::thread::spawn({
            let vmo = vmo.clone();
            move || {
                let mut data = [0u8; 100];
                vmo.read(&mut data, 0).unwrap();
            }
        });

        let packet = port.wait(zx::Time::INFINITE).unwrap();
        assert_eq!(packet.key(), KEY);
        match packet.contents() {
            zx::PacketContents::Pager(request) => {
                assert_eq!(
                    request.command(),
                    zx::sys::zx_page_request_command_t::ZX_PAGER_VMO_READ
                );
                assert_eq!(request.range(), 0..page_size);
                let aux_vmo = zx::Vmo::create(page_size).unwrap();
                pager.supply_pages(vmo.as_ref(), request.range(), &aux_vmo, 0).unwrap();
            }
            packet => panic!("Unexpected packet: {:?}", packet),
        }
        read_thread.join().unwrap();
    }

    #[test]
    fn fail_page_request() {
        let page_size: u64 = zx::system_get_page_size().into();
        let port = zx::Port::create().unwrap();
        let pager = zx::Pager::create(zx::PagerOptions::empty()).unwrap();
        let vmo =
            Arc::new(pager.create_vmo(zx::VmoOptions::empty(), &port, KEY, page_size).unwrap());

        let read_thread = std::thread::spawn({
            let vmo = vmo.clone();
            move || {
                let mut data = [0u8; 100];
                let e = vmo.read(&mut data, 0).expect_err("Request should have failed");
                assert_eq!(e, zx::Status::NO_SPACE);
            }
        });

        let packet = port.wait(zx::Time::INFINITE).unwrap();
        assert_eq!(packet.key(), KEY);
        match packet.contents() {
            zx::PacketContents::Pager(request) => {
                assert_eq!(
                    request.command(),
                    zx::sys::zx_page_request_command_t::ZX_PAGER_VMO_READ
                );
                assert_eq!(request.range(), 0..page_size);
                pager
                    .op_range(
                        zx::PagerOp::Fail(zx::Status::NO_SPACE),
                        vmo.as_ref(),
                        request.range(),
                    )
                    .unwrap();
            }
            packet => panic!("Unexpected packet: {:?}", packet),
        }
        read_thread.join().unwrap();
    }

    #[test]
    fn pager_writeback() {
        let page_size: u64 = zx::system_get_page_size().into();
        let port = zx::Port::create().unwrap();
        let pager = zx::Pager::create(zx::PagerOptions::empty()).unwrap();
        let vmo =
            Arc::new(pager.create_vmo(zx::VmoOptions::TRAP_DIRTY, &port, KEY, page_size).unwrap());

        let write_thread = std::thread::spawn({
            let vmo = vmo.clone();
            move || {
                let data = [0u8; 100];
                vmo.write(&data, 0).unwrap();
            }
        });

        let packet = port.wait(zx::Time::INFINITE).unwrap();
        assert_eq!(packet.key(), KEY);
        match packet.contents() {
            zx::PacketContents::Pager(request) => {
                assert_eq!(
                    request.command(),
                    zx::sys::zx_page_request_command_t::ZX_PAGER_VMO_READ
                );
                assert_eq!(request.range(), 0..page_size);
                let aux_vmo = zx::Vmo::create(page_size).unwrap();
                pager.supply_pages(vmo.as_ref(), request.range(), &aux_vmo, 0).unwrap();
            }
            packet => panic!("Unexpected packet: {:?}", packet),
        }

        let packet = port.wait(zx::Time::INFINITE).unwrap();
        assert_eq!(packet.key(), KEY);
        match packet.contents() {
            zx::PacketContents::Pager(request) => {
                assert_eq!(
                    request.command(),
                    zx::sys::zx_page_request_command_t::ZX_PAGER_VMO_DIRTY
                );
                assert_eq!(request.range(), 0..page_size);
                pager.op_range(zx::PagerOp::Dirty, vmo.as_ref(), request.range()).unwrap();
            }
            packet => panic!("Unexpected packet: {:?}", packet),
        }

        write_thread.join().unwrap();

        pager.op_range(zx::PagerOp::WritebackBegin, vmo.as_ref(), 0..page_size).unwrap();
        pager.op_range(zx::PagerOp::WritebackEnd, vmo.as_ref(), 0..page_size).unwrap();
    }
}
