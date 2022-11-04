// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{send_scan_complete, send_scan_result, BeaconInfo},
    fidl_fuchsia_wlan_tap as wlantap, fuchsia_zircon as zx,
    paste::paste,
    wlan_common::{
        buffer_reader::BufferReader,
        mac::{FrameControl, FrameType},
    },
};

pub fn start_scan_handler<'a>(
    phy: &'a wlantap::WlantapPhyProxy,
    scan_results: Result<Vec<BeaconInfo>, zx::Status>,
) -> Box<dyn FnMut(&wlantap::StartScanArgs) + 'a> {
    match scan_results {
        Ok(scan_results) => {
            Box::new(move |&wlantap::StartScanArgs { wlan_softmac_id: _, scan_id }| {
                for beacon_info in &scan_results {
                    send_scan_result(&phy, beacon_info);
                }
                send_scan_complete(scan_id, zx::Status::OK.into_raw(), &phy).unwrap();
            })
        }
        Err(status) => Box::new(move |&wlantap::StartScanArgs { wlan_softmac_id: _, scan_id }| {
            send_scan_complete(scan_id, status.into_raw(), &phy).unwrap();
        }),
    }
}

pub struct TxHandlerBuilder<'a> {
    mgmt_frame_handler: Box<dyn FnMut(&Vec<u8>) + 'a>,
    ctrl_frame_handler: Box<dyn FnMut(&Vec<u8>) + 'a>,
    data_frame_handler: Box<dyn FnMut(&Vec<u8>) + 'a>,
    ext_frame_handler: Box<dyn FnMut(&Vec<u8>) + 'a>,
    other_frame_handler: Box<dyn FnMut(&Vec<u8>) + 'a>,
}

macro_rules! on_frame {
    ($frame_type:ident) => {
        paste! {
            pub fn [<on_ $frame_type _frame>](mut self, handler: impl FnMut(& Vec<u8>) + 'a) -> Self {
                self.[<$frame_type _frame_handler>] = Box::new(handler);
                self
            }
        }
    };
}

impl<'a> TxHandlerBuilder<'a> {
    pub fn new() -> Self {
        Self {
            mgmt_frame_handler: Box::new(|_| {}),
            ctrl_frame_handler: Box::new(|_| {}),
            data_frame_handler: Box::new(|_| {}),
            ext_frame_handler: Box::new(|_| {}),
            other_frame_handler: Box::new(|_| {}),
        }
    }

    on_frame!(mgmt);
    on_frame!(ctrl);
    on_frame!(data);
    on_frame!(ext);
    on_frame!(other);

    pub fn build(self) -> Box<dyn FnMut(&wlantap::TxArgs) + 'a> {
        let mut mgmt_frame_handler = self.mgmt_frame_handler;
        let mut ctrl_frame_handler = self.ctrl_frame_handler;
        let mut data_frame_handler = self.data_frame_handler;
        let mut ext_frame_handler = self.ext_frame_handler;
        let mut other_frame_handler = self.other_frame_handler;
        Box::new(move |tx_args| {
            let packet = &tx_args.packet;
            let reader = BufferReader::new(&packet.data[..]);
            let fc = FrameControl(reader.peek_value().unwrap());

            match fc.frame_type() {
                FrameType::MGMT => mgmt_frame_handler(&packet.data),
                FrameType::CTRL => ctrl_frame_handler(&packet.data),
                FrameType::DATA => data_frame_handler(&packet.data),
                FrameType::EXT => ext_frame_handler(&packet.data),
                _ => other_frame_handler(&packet.data),
            }
        })
    }
}

/// EventHandlerBuilder builds a action that can be passed to
/// `TestHelper::run_until_complete_or_timeout`.
pub struct EventHandlerBuilder<'a> {
    set_channel_handler: Box<dyn FnMut(&wlantap::SetChannelArgs) + 'a>,
    start_scan_handler: Box<dyn FnMut(&wlantap::StartScanArgs) + 'a>,
    tx_handler: Box<dyn FnMut(&wlantap::TxArgs) + 'a>,
    phy_event_handler: Box<dyn FnMut(&wlantap::WlantapPhyEvent) + 'a>,
    debug_name: Option<String>,
}

impl<'a> EventHandlerBuilder<'a> {
    pub fn new() -> Self {
        Self {
            set_channel_handler: Box::new(|_| {}),
            start_scan_handler: Box::new(|_| {}),
            tx_handler: Box::new(|_| {}),
            phy_event_handler: Box::new(|_| {}),
            debug_name: None,
        }
    }

    pub fn on_set_channel(mut self, handler: impl FnMut(&wlantap::SetChannelArgs) + 'a) -> Self {
        self.set_channel_handler = Box::new(handler);
        self
    }

    pub fn on_start_scan(mut self, handler: impl FnMut(&wlantap::StartScanArgs) + 'a) -> Self {
        self.start_scan_handler = Box::new(handler);
        self
    }

    pub fn on_tx(mut self, handler: impl FnMut(&wlantap::TxArgs) + 'a) -> Self {
        self.tx_handler = Box::new(handler);
        self
    }

    /// Sets the handler for all PHY events. Only one may be registered.
    ///
    /// The handler registered here will be called even if another handler has already been called:
    /// e.g. if a SetChannel event was received, both the on_set_channel and on_phy_event handlers
    /// will be called.
    pub fn on_phy_event(mut self, handler: impl FnMut(&wlantap::WlantapPhyEvent) + 'a) -> Self {
        self.phy_event_handler = Box::new(handler);
        self
    }

    // TODO(fxbug.dev/91118) - Added to help investigate hw-sim test. Remove later
    pub fn on_debug_name(mut self, debug_name: &str) -> Self {
        self.debug_name = Some(debug_name.to_string());
        self
    }

    pub fn build(mut self) -> impl FnMut(wlantap::WlantapPhyEvent) + 'a {
        move |event| {
            match event {
                wlantap::WlantapPhyEvent::SetChannel { ref args } => {
                    (self.set_channel_handler)(args)
                }

                wlantap::WlantapPhyEvent::Tx { ref args } => {
                    // TODO(fxbug.dev/91118) - Added to help investigate hw-sim test. Remove later
                    if let Some(debug_name) = &self.debug_name {
                        tracing::info!(
                            "[{}] - Tx({} bytes): {:X?}",
                            debug_name,
                            args.packet.data.len(),
                            args.packet.data
                        );
                    }
                    (self.tx_handler)(args)
                }

                wlantap::WlantapPhyEvent::StartScan { ref args } => (self.start_scan_handler)(args),

                _ => {}
            }
            (self.phy_event_handler)(&event)
        }
    }
}
