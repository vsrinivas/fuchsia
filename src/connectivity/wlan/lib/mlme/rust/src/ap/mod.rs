// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod utils;

use {
    crate::{
        buffer::{BufferProvider, OutBuf},
        device::{Device, TxFlags},
        error::Error,
    },
    wlan_common::{
        buffer_writer::BufferWriter,
        frame_len,
        mac::{self, Bssid, MacAddr, StatusCode},
        sequence::SequenceManager,
    },
};

pub use utils::*;

pub struct Ap {
    device: Device,
    buf_provider: BufferProvider,
    seq_mgr: SequenceManager,
    bssid: Bssid,
}

impl Ap {
    pub fn new(device: Device, buf_provider: BufferProvider, bssid: Bssid) -> Self {
        Self { device, buf_provider, seq_mgr: SequenceManager::new(), bssid }
    }

    pub fn bssid(&self) -> Bssid {
        self.bssid
    }

    pub fn send_open_auth_frame(
        &mut self,
        client_addr: MacAddr,
        status_code: StatusCode,
    ) -> Result<(), Error> {
        const FRAME_LEN: usize = frame_len!(mac::MgmtHdr, mac::AuthHdr);
        let mut buf = self.buf_provider.get_buffer(FRAME_LEN)?;
        let mut w = BufferWriter::new(&mut buf[..]);
        write_open_auth_frame(&mut w, client_addr, self.bssid, &mut self.seq_mgr, status_code)?;
        let bytes_written = w.bytes_written();
        let out_buf = OutBuf::from(buf, bytes_written);
        self.device
            .send_wlan_frame(out_buf, TxFlags::NONE)
            .map_err(|s| Error::Status(format!("error sending open auth frame"), s))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{buffer::FakeBufferProvider, device::FakeDevice},
    };
    const CLIENT_ADDR: MacAddr = [6u8; 6];
    const BSSID: Bssid = Bssid([7u8; 6]);

    fn make_ap_station(device: Device) -> Ap {
        let buf_provider = FakeBufferProvider::new();
        let ap = Ap::new(device, buf_provider, BSSID);
        ap
    }

    #[test]
    fn ap_send_open_auth_frame() {
        let mut fake_device = FakeDevice::new();
        let mut ap = make_ap_station(fake_device.as_device());
        ap.send_open_auth_frame(CLIENT_ADDR, StatusCode::TRANSACTION_SEQUENCE_ERROR)
            .expect("error delivering WLAN frame");
        assert_eq!(fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&fake_device.wlan_queue[0].0[..], &[
            // Mgmt header:
            0b1011_00_00, 0b00000000, // FC
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // addr1
            7, 7, 7, 7, 7, 7, // addr2
            7, 7, 7, 7, 7, 7, // addr3
            0x10, 0, // Sequence Control
            // Auth header:
            0, 0, // auth algorithm
            2, 0, // auth txn seq num
            14, 0, // Status code
        ][..]);
    }
}
