// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Data for testing parsing/serialization of IGMP.
//!
//! This data is a mix of hand-crafted buffers and network captures.

pub(crate) mod igmp_router_queries {
    pub(crate) mod v2 {
        pub(crate) const QUERY: &[u8] = &[0x11, 0x64, 0xee, 0x9b, 0x00, 0x00, 0x00, 0x00];

        pub(crate) const HOST_GROUP_ADDRESS: [u8; 4] = [0, 0, 0, 0];
        pub(crate) const MAX_RESP_CODE: u8 = 100;
    }

    pub(crate) mod v3 {
        pub(crate) const QUERY: &[u8] = &[
            0x11, 0x64, 0x24, 0x64, 0xe0, 0x00, 0x00, 0x01, 0x0A, 0x32, 0x00, 0x01, 0xe0, 0x00,
            0x00, 0x02,
        ];

        pub(crate) const MAX_RESP_CODE: u8 = 100;
        pub(crate) const GROUP_ADDRESS: [u8; 4] = [224, 0, 0, 1];
        pub(crate) const SUPPRESS_ROUTER_SIDE: bool = true;
        pub(crate) const QRV: u8 = 0x02;
        pub(crate) const QQIC_SECS: u32 = 50;
        pub(crate) const NUMBER_OF_SOURCES: u16 = 1;
        pub(crate) const SOURCE: [u8; 4] = [224, 0, 0, 2];
    }
}

pub(crate) mod igmp_reports {
    pub(crate) mod v1 {
        pub(crate) const MEMBER_REPORT: &[u8] = &[0x12, 0x00, 0x0d, 0xfe, 0xe0, 0x00, 0x00, 0x01];

        pub(crate) const GROUP_ADDRESS: [u8; 4] = [224, 0, 0, 1];
    }

    pub(crate) mod v2 {
        pub(crate) const MEMBER_REPORT: &[u8] = &[0x16, 0x00, 0x09, 0xfe, 0xe0, 0x00, 0x00, 0x01];

        pub(crate) const GROUP_ADDRESS: [u8; 4] = [224, 0, 0, 1];
    }

    pub(crate) mod v3 {
        pub(crate) const MEMBER_REPORT: &[u8] = &[
            0x22, 0x00, 0x7a, 0xe7, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x02, 0xe0, 0x00,
            0x00, 0x01, 0xe0, 0x00, 0x00, 0x02, 0xe0, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x01,
            0xe0, 0x00, 0x00, 0x04, 0xe0, 0x00, 0x00, 0x05,
        ];

        pub(crate) const NUMBER_OF_RECORDS: u16 = 2;
        pub(crate) const MAX_RESP_CODE: u8 = 0;
        pub(crate) const NUMBER_OF_SOURCES_1: u16 = 2;
        pub(crate) const RECORD_TYPE_1: u8 = 1;
        pub(crate) const MULTICAST_ADDR_1: [u8; 4] = [224, 0, 0, 1];
        pub(crate) const SRC_1_1: [u8; 4] = [224, 0, 0, 2];
        pub(crate) const SRC_1_2: [u8; 4] = [224, 0, 0, 3];
        pub(crate) const NUMBER_OF_SOURCES_2: u16 = 1;
        pub(crate) const RECORD_TYPE_2: u8 = 2;
        pub(crate) const MULTICAST_ADDR_2: [u8; 4] = [224, 0, 0, 4];
        pub(crate) const SRC_2_1: [u8; 4] = [224, 0, 0, 5];
    }
}

pub(crate) mod igmp_leave_group {
    pub(crate) const LEAVE_GROUP: &[u8] = &[0x17, 0x00, 0x08, 0xfe, 0xe0, 0x00, 0x00, 0x01];

    pub(crate) const GROUP_ADDRESS: [u8; 4] = [224, 0, 0, 1];
}

pub(crate) mod igmp_invalid_buffers {
    pub(crate) const UNKNOWN_TYPE: &[u8] = &[0xF0, 0x00, 0x08, 0xfe, 0xe0, 0x00, 0x00, 0x01];
}
