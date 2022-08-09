// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{ok, AsHandleRef, Guest, Handle, HandleBased, HandleRef, Packet, Status},
    fuchsia_zircon_sys as sys,
};

#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Vcpu(Handle);
impl_handle_based!(Vcpu);

impl Vcpu {
    /// Create a VCPU, for use with `guest` that begins execution at `entry`.
    pub fn create(guest: &Guest, entry: usize) -> Result<Vcpu, Status> {
        unsafe {
            let mut vcpu_handle = 0;
            ok(sys::zx_vcpu_create(guest.raw_handle(), 0, entry, &mut vcpu_handle))?;
            Ok(Self::from(Handle::from_raw(vcpu_handle)))
        }
    }

    /// Enter a VCPU, causing it to resume execution.
    pub fn enter(&self) -> Result<Packet, Status> {
        let mut packet = Default::default();
        ok(unsafe { sys::zx_vcpu_enter(self.raw_handle(), &mut packet) })?;
        Ok(Packet(packet))
    }

    /// Kick a VCPU, causing it to stop execution.
    pub fn kick(&self) -> Result<(), Status> {
        ok(unsafe { sys::zx_vcpu_kick(self.raw_handle()) })
    }

    /// Raise an interrupt on a VCPU.
    pub fn interrupt(&self, vector: u32) -> Result<(), Status> {
        ok(unsafe { sys::zx_vcpu_interrupt(self.raw_handle(), vector) })
    }

    /// Read the state of a VCPU.
    pub fn read_state(&self) -> Result<sys::zx_vcpu_state_t, Status> {
        let mut state = sys::zx_vcpu_state_t::default();
        let status = unsafe {
            sys::zx_vcpu_read_state(
                self.raw_handle(),
                sys::ZX_VCPU_STATE,
                &mut state as *mut _ as *mut u8,
                std::mem::size_of_val(&state),
            )
        };
        ok(status).map(|_| state)
    }

    /// Write the state of a VCPU.
    pub fn write_state(&self, state: &sys::zx_vcpu_state_t) -> Result<(), Status> {
        let status = unsafe {
            sys::zx_vcpu_write_state(
                self.raw_handle(),
                sys::ZX_VCPU_STATE,
                state as *const _ as *const u8,
                std::mem::size_of_val(state),
            )
        };
        ok(status)
    }
}

#[derive(Debug, Clone, Copy)]
pub enum VcpuContents {
    Interrupt { mask: u64, vector: u8 },
    Startup { id: u64, entry: sys::zx_gpaddr_t },
    Exit { retcode: i64 },
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::Resource, fidl_fuchsia_kernel as fkernel,
        fuchsia_component::client::connect_to_protocol, fuchsia_zircon::HandleBased,
    };

    async fn get_hypervisor() -> Resource {
        let resource = connect_to_protocol::<fkernel::HypervisorResourceMarker>()
            .unwrap()
            .get()
            .await
            .unwrap();
        unsafe { Resource::from(Handle::from_raw(resource.into_raw())) }
    }

    #[fuchsia::test]
    async fn vcpu_normal_create() {
        let hypervisor = get_hypervisor().await;
        let (guest, _vmar) = match Guest::normal(&hypervisor) {
            Err(Status::NOT_SUPPORTED) => {
                println!("Hypervisor not supported");
                return;
            }
            result => result.unwrap(),
        };
        let _vcpu = Vcpu::create(&guest, 0).unwrap();
    }

    #[fuchsia::test]
    async fn vcpu_normal_interrupt() {
        let hypervisor = get_hypervisor().await;
        let (guest, _vmar) = match Guest::normal(&hypervisor) {
            Err(Status::NOT_SUPPORTED) => {
                println!("Hypervisor not supported");
                return;
            }
            result => result.unwrap(),
        };
        let vcpu = Vcpu::create(&guest, 0).unwrap();

        vcpu.interrupt(0).unwrap();
    }

    #[fuchsia::test]
    async fn vcpu_normal_read_write_state() {
        let hypervisor = get_hypervisor().await;
        let (guest, _vmar) = match Guest::normal(&hypervisor) {
            Err(Status::NOT_SUPPORTED) => {
                println!("Hypervisor not supported");
                return;
            }
            result => result.unwrap(),
        };
        let vcpu = Vcpu::create(&guest, 0).unwrap();

        let state = vcpu.read_state().unwrap();
        vcpu.write_state(&state).unwrap();
    }

    #[fuchsia::test]
    async fn vcpu_direct_create() {
        let hypervisor = get_hypervisor().await;
        let (guest, _vmar) = match Guest::direct(&hypervisor) {
            Err(Status::NOT_SUPPORTED) => {
                println!("Hypervisor not supported");
                return;
            }
            result => result.unwrap(),
        };
        let _vcpu = Vcpu::create(&guest, 0).unwrap();
    }

    #[fuchsia::test]
    async fn vcpu_direct_interrupt() {
        let hypervisor = get_hypervisor().await;
        let (guest, _vmar) = match Guest::direct(&hypervisor) {
            Err(Status::NOT_SUPPORTED) => {
                println!("Hypervisor not supported");
                return;
            }
            result => result.unwrap(),
        };
        let vcpu = Vcpu::create(&guest, 0).unwrap();

        match vcpu.interrupt(0) {
            Err(Status::NOT_SUPPORTED) => (),
            _ => panic!("Interrupt should not be supported"),
        }
    }

    #[fuchsia::test]
    async fn vcpu_direct_read_write_state() {
        let hypervisor = get_hypervisor().await;
        let (guest, _vmar) = match Guest::direct(&hypervisor) {
            Err(Status::NOT_SUPPORTED) => {
                println!("Hypervisor not supported");
                return;
            }
            result => result.unwrap(),
        };
        let vcpu = Vcpu::create(&guest, 0).unwrap();

        let state = vcpu.read_state().unwrap();
        vcpu.write_state(&state).unwrap();
    }
}
