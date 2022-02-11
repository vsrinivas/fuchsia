// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// DNS-SD Service Name for TREL
pub const TREL_DNSSD_SERVICE_NAME: &'static str = "_trel._udp.";

/// Methods from the [OpenThread TREL Module][1].
///
/// [1]: https://openthread.io/reference/group/api-trel
pub trait Trel {
    /// Enables or disables TREL operation.
    fn trel_set_enabled(&self, enabled: bool);

    /// Returns true if TREL is enabled.
    fn trel_is_enabled(&self) -> bool;
}

impl<T: Trel + Boxable> Trel for ot::Box<T> {
    fn trel_set_enabled(&self, enabled: bool) {
        self.as_ref().trel_set_enabled(enabled);
    }

    fn trel_is_enabled(&self) -> bool {
        self.as_ref().trel_is_enabled()
    }
}

impl Trel for Instance {
    fn trel_set_enabled(&self, enabled: bool) {
        if enabled {
            unsafe { otTrelEnable(self.as_ot_ptr()) }
        } else {
            unsafe { otTrelDisable(self.as_ot_ptr()) }
        }
    }

    fn trel_is_enabled(&self) -> bool {
        unsafe { otTrelIsEnabled(self.as_ot_ptr()) }
    }
}

/// Functional equivalent of [`otsys::otPlatTrelPeerInfo`](crate::otsys::otPlatTrelPeerInfo).
#[derive(Clone)]
#[repr(transparent)]
pub struct PlatTrelPeerInfo<'a>(
    otPlatTrelPeerInfo,
    PhantomData<*mut otMessage>,
    PhantomData<&'a ()>,
);
impl_ot_castable!(
    lifetime
    PlatTrelPeerInfo<'_>,
    otPlatTrelPeerInfo,
    PhantomData::default(),
    PhantomData::default()
);

impl std::fmt::Debug for PlatTrelPeerInfo<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PlatTrelPeerInfo")
            .field("removed", &self.is_removed())
            .field("txt", &self.txt_escaped())
            .field("sockaddr", &self.sockaddr())
            .finish()
    }
}

impl<'a> PlatTrelPeerInfo<'a> {
    /// Creates a new instance of `PlatTrelPeerInfo`.
    pub fn new(removed: bool, txt: &[u8], sockaddr: ot::SockAddr) -> PlatTrelPeerInfo<'_> {
        PlatTrelPeerInfo::from_ot(otPlatTrelPeerInfo {
            mRemoved: removed,
            mTxtData: txt.as_ptr(),
            mTxtLength: txt.len().try_into().unwrap(),
            mSockAddr: sockaddr.into_ot(),
        })
    }

    /// Returns true if this peer is being removed.
    pub fn is_removed(&self) -> bool {
        self.0.mRemoved
    }

    /// Returns the raw value of the TXT field.
    pub fn txt(&self) -> &'a [u8] {
        unsafe { core::slice::from_raw_parts(self.0.mTxtData, self.0.mTxtLength.into()) }
    }

    /// Returns the TXT field as an escaped ASCII string.
    pub fn txt_escaped(&self) -> String {
        self.txt()
            .into_iter()
            .map(Clone::clone)
            .map(std::ascii::escape_default)
            .flatten()
            .map(char::from)
            .collect::<String>()
    }

    /// Returns the SockAddr for this peer.
    pub fn sockaddr(&self) -> SockAddr {
        SockAddr::from_ot(self.0.mSockAddr)
    }
}

/// Platform methods from the [OpenThread TREL Module][1].
///
/// [1]: https://openthread.io/reference/group/plat-trel
pub trait PlatTrel {
    /// This function is a callback from platform to notify of a received TREL UDP packet.
    fn plat_trel_handle_received(&self, packet: &[u8]);

    /// This is a callback function from platform layer to report a discovered TREL peer info.
    fn plat_trel_handle_discovered_peer_info(&self, peer_info: &PlatTrelPeerInfo<'_>);
}

impl<T: PlatTrel + Boxable> PlatTrel for ot::Box<T> {
    fn plat_trel_handle_received(&self, packet: &[u8]) {
        self.as_ref().plat_trel_handle_received(packet);
    }

    fn plat_trel_handle_discovered_peer_info(&self, peer_info: &PlatTrelPeerInfo<'_>) {
        self.as_ref().plat_trel_handle_discovered_peer_info(peer_info);
    }
}

impl PlatTrel for Instance {
    fn plat_trel_handle_received(&self, packet: &[u8]) {
        unsafe {
            otPlatTrelHandleReceived(
                self.as_ot_ptr(),
                // TODO(fxbug.dev/93684): Make sure they won't actually mutate.
                packet.as_ptr() as *mut u8,
                packet.len().try_into().unwrap(),
            )
        }
    }

    fn plat_trel_handle_discovered_peer_info(&self, peer_info: &PlatTrelPeerInfo<'_>) {
        unsafe { otPlatTrelHandleDiscoveredPeerInfo(self.as_ot_ptr(), peer_info.as_ot_ptr()) }
    }
}
