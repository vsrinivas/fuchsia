// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Data for testing parsing/serialization of IGMP.
//!
//! This data is a mix of hand-crafted buffers and network captures.

pub mod igmp_router_queries {
    pub mod v2 {
        pub const QUERY: &[u8] = &[0x11, 0x64, 0xee, 0x9b, 0x00, 0x00, 0x00, 0x00];

        pub const HOST_GROUP_ADDRESS: [u8; 4] = [0, 0, 0, 0];
        pub const MAX_RESP_CODE: u8 = 100;
    }

    pub mod v3 {
        pub const QUERY: &[u8] = &[
            0x11, 0x64, 0x24, 0x64, 0xe0, 0x00, 0x00, 0x01, 0x0A, 0x32, 0x00, 0x01, 0xe0, 0x00,
            0x00, 0x02,
        ];

        pub const MAX_RESP_CODE: u8 = 100;
        pub const GROUP_ADDRESS: [u8; 4] = [224, 0, 0, 1];
        pub const SUPPRESS_ROUTER_SIDE: bool = true;
        pub const QRV: u8 = 0x02;
        pub const QQIC_SECS: u32 = 50;
        pub const NUMBER_OF_SOURCES: u16 = 1;
        pub const SOURCE: [u8; 4] = [224, 0, 0, 2];
    }
}

pub mod igmp_reports {
    pub mod v3 {
        pub const MEMBER_REPORT: &[u8] = &[
            0x22, 0x00, 0x7a, 0xe7, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x02, 0xe0, 0x00,
            0x00, 0x01, 0xe0, 0x00, 0x00, 0x02, 0xe0, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x01,
            0xe0, 0x00, 0x00, 0x04, 0xe0, 0x00, 0x00, 0x05,
        ];

        pub const NUMBER_OF_RECORDS: u16 = 2;
        pub const MAX_RESP_CODE: u8 = 0;
        pub const NUMBER_OF_SOURCES_1: u16 = 2;
        pub const RECORD_TYPE_1: u8 = 1;
        pub const MULTICAST_ADDR_1: [u8; 4] = [224, 0, 0, 1];
        pub const SRC_1_1: [u8; 4] = [224, 0, 0, 2];
        pub const SRC_1_2: [u8; 4] = [224, 0, 0, 3];
        pub const NUMBER_OF_SOURCES_2: u16 = 1;
        pub const RECORD_TYPE_2: u8 = 2;
        pub const MULTICAST_ADDR_2: [u8; 4] = [224, 0, 0, 4];
        pub const SRC_2_1: [u8; 4] = [224, 0, 0, 5];
    }
}
