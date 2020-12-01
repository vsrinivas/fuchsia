// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{Header, Id},
    crate::buffer_reader::BufferReader,
    std::{
        collections::{btree_map, BTreeMap},
        convert::TryInto,
        hash::Hash,
        mem::size_of,
        ops::Range,
    },
    zerocopy::ByteSlice,
};

const IES_MERGER_BUFFER_LIMIT: usize = 10000;

/// IesMerger is intended to be used to merge beacon and probe response IEs from scan results, as
/// some IEs are only available on one frame type, or an IE may exist on both but contains more
/// information on one frame type over the other.
///
/// IesMerger therefore has two purposes:
/// 1. Combine unique IEs from each one into a single IE block.
/// 2. Pick which IE to keep when the same type is available in multiple IE blocks but has
///    different information:
///    a. IesMerger knows about and uses custom logic for specific IE (e.g. for SSID, we
///       prioritize non-hidden SSID over hidden SSID).
///    b. In the general case, IesMerger keeps the IE that has more bytes (known use case:
///       for WPS IE, the one in probe response contains more information).
///    c. If the number of bytes are the same, IesMerger keeps the later one.
#[derive(Debug)]
pub struct IesMerger {
    // An index of the IEs we are going to keep. Mapping from IeType to the range of the
    // corresponding bytes in the underlying buffer.
    ies_summaries: BTreeMap<IeType, Range<usize>>,
    ies_buf: Vec<u8>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum IeType {
    Ieee {
        id: Id,
        extension: Option<u8>,
    },
    Vendor {
        vendor_ie_hdr: [u8; 6], // OUI, OUI type, version
    },
}

impl IeType {
    fn id(&self) -> Id {
        match self {
            IeType::Ieee { id, .. } => *id,
            IeType::Vendor { .. } => Id::VENDOR_SPECIFIC,
        }
    }
}

impl std::cmp::PartialOrd for IeType {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl std::cmp::Ord for IeType {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        match (self, other) {
            (
                IeType::Ieee { extension: Some(ext_id), .. },
                IeType::Ieee { extension: Some(other_ext_id), .. },
            ) => ext_id.cmp(other_ext_id),
            (IeType::Vendor { vendor_ie_hdr }, IeType::Vendor { vendor_ie_hdr: other_hdr }) => {
                vendor_ie_hdr.cmp(other_hdr)
            }
            _ => self.id().0.cmp(&other.id().0),
        }
    }
}

impl IesMerger {
    pub fn new(ies: Vec<u8>) -> Self {
        let mut ies_summaries = BTreeMap::new();
        for (ie_type, range) in IeSummaryIter::new(&ies[..]) {
            ies_summaries.insert(ie_type, range);
        }
        Self { ies_summaries, ies_buf: ies }
    }

    pub fn merge(&mut self, ies: &[u8]) {
        for (ie_type, range) in IeSummaryIter::new(ies) {
            let add_new = match self.ies_summaries.entry(ie_type) {
                btree_map::Entry::Occupied(entry) => {
                    should_add_new(ie_type, &self.ies_buf[entry.get().clone()], &ies[range.clone()])
                }
                btree_map::Entry::Vacant(_) => true,
            };
            let new_addition_len = range.end - range.start;
            // IesMerger has a buffer limit so an unfriendly AP can't cause us to run out of
            // memory by repeatedly changing the IEs.
            if add_new && self.ies_buf.len() + new_addition_len <= IES_MERGER_BUFFER_LIMIT {
                let start_idx = self.ies_buf.len();
                self.ies_buf.extend_from_slice(&ies[range]);
                self.ies_summaries.insert(ie_type, start_idx..self.ies_buf.len());
            }
        }
    }

    /// Build and return merged IEs, sorted by order of IE ID.
    pub fn finalize(&mut self) -> Vec<u8> {
        let total_len = self.ies_summaries.values().map(|r| r.end - r.start).sum();
        let mut ies = Vec::with_capacity(total_len);
        for range in self.ies_summaries.values() {
            ies.extend_from_slice(&self.ies_buf[range.clone()]);
        }
        ies
    }
}

fn should_add_new(ie_type: IeType, old: &[u8], new: &[u8]) -> bool {
    // If both IEs are the same, no need to add new IE
    if old == new {
        return false;
    }

    // If the new SSID is blank (hidden SSID), prefer the old one
    if ie_type.id() == Id::SSID {
        if new.len() < size_of::<Header>() {
            return false;
        }
        let ssid = &new[2..];
        if ssid.iter().all(|c| *c == 0u8) {
            return false;
        }
    }

    // In general case, prioritize the one with more information.
    // For example, for WPS vendor IE, the AP sends a short one in the beacon and
    // a longer one in the probe response.
    //
    // If they are the same length but different, then prioritize the new one.
    new.len() >= old.len()
}

struct IeSummaryIter<B>(BufferReader<B>);

impl<B: ByteSlice> IeSummaryIter<B> {
    pub fn new(bytes: B) -> Self {
        Self(BufferReader::new(bytes))
    }
}

impl<B: ByteSlice> Iterator for IeSummaryIter<B> {
    type Item = (IeType, Range<usize>);

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let start_idx = self.0.bytes_read();
            let header = self.0.peek::<Header>()?;
            let body_len = header.body_len as usize;

            // There are not enough bytes left, return None.
            if self.0.bytes_remaining() < size_of::<Header>() + body_len {
                return None;
            }

            // Unwraps are OK because we checked the length above.
            let header = self.0.read::<Header>().unwrap();
            let body = self.0.read_bytes(body_len).unwrap();
            let ie_type = match header.id {
                Id::VENDOR_SPECIFIC => {
                    if body.len() >= 6 {
                        Some(IeType::Vendor { vendor_ie_hdr: body[0..6].try_into().unwrap() })
                    } else {
                        None
                    }
                }
                Id::EXTENSION => {
                    if body.len() >= 1 {
                        Some(IeType::Ieee { id: header.id, extension: Some(body[0]) })
                    } else {
                        None
                    }
                }
                _ => Some(IeType::Ieee { id: header.id, extension: None }),
            };
            // If IE type is valid, return the IE block. Otherwise, skip to the next one.
            match ie_type {
                Some(ie_type) => {
                    return Some((ie_type, start_idx..start_idx + size_of::<Header>() + body_len))
                }
                None => (),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // The beacon IEs here and the probe response IEs below are taken from the same router, with
    // the following modification:
    // 1. SSID is changed
    // 2. Order of IEs are sorted according to `IeType` ordering to make test code simpler.
    #[rustfmt::skip]
    const BEACON_FRAME_IES: &'static [u8] = &[
        // SSID: "foo-ssid"
        0x00, 0x08, 0x66, 0x6f, 0x6f, 0x2d, 0x73, 0x73, 0x69, 0x64,
        // Supported Rates: 6(B), 9, 12(B), 18, 24(B), 36, 48, 54, [Mbit/sec]
        0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
        // DS Parameter set: Current Channel: 157
        0x03, 0x01, 0x9d,
        // Traffic Indication Map (TIM): DTIM 0 of 0 bitmap
        0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
        // Power Constraint: 3
        0x20, 0x01, 0x03,
        // HT Capabilities (802.11n D1.10)
        0x2d, 0x1a,
        0xef, 0x09, // HT Capabilities Info
        0x1b, // A-MPDU Parameters: 0x1b
        0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // MCS Set
        0x00, 0x00, // HT Extended Capabilities
        0x00, 0x00, 0x00, 0x00, // Transmit Beamforming Capabilities
        0x00, // Antenna Selection Capabilities
        // RSN Information
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04, // Group Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, // Pairwise Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, // AKM: PSK
        0x00, 0x00, // RSN Capabilities
        // HT Information (802.11n D1.10)
        0x3d, 0x16,
        0x9d, // Primary Channel: 157
        0x0d, // HT Info Subset - secondary channel above, any channel width, RIFS permitted
        0x00, 0x00, 0x00, 0x00, // HT Info Subsets
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Basic MCS Set
        // Overlapping BSS Scan Parameters
        0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
        // Extended Capabilities
        0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
        // VHT Capabilities
        0xbf, 0x0c,
        0xb2, 0x01, 0x80, 0x33, // VHT Capabilities Info
        0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT Supported MCS Set
        // VHT Operation
        0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
        // VHT Tx Power Envelope
        0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
        // Vendor Specific: Atheros Communications, Inc.: Advanced Capability
        0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
        // Vendor Specific: Microsoft Corp.: WMM/WME: Parameter Element
        0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
        0x80, // U-APSD enabled
        0x00,
        0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
        0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
        0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
        0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        // Vendor Specific: Microsoft Corp.: WPS
        0xdd, 0x1d, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01,
        0x02, 0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01,
        0x20,
    ];

    #[rustfmt::skip]
    const PROBE_RESP_IES: &'static [u8] = &[
        // SSID: "foo-ssid"
        0x00, 0x08, 0x66, 0x6f, 0x6f, 0x2d, 0x73, 0x73, 0x69, 0x64,
        // Supported Rates: 6(B), 9, 12(B), 18, 24(B), 36, 48, 54, [Mbit/sec]
        0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
        // DS Parameter set: Current Channel: 157
        0x03, 0x01, 0x9d,
        // Power Constraint: 3
        0x20, 0x01, 0x03,
        // HT Capabilities (802.11n D1.10)
        0x2d, 0x1a,
        0xef, 0x09, // HT Capabilities Info
        0x1b, // A-MPDU Parameters: 0x1b
        0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // MCS Set
        0x00, 0x00, // HT Extended Capabilities
        0x00, 0x00, 0x00, 0x00, // Transmit Beamforming Capabilities
        0x00, // Antenna Selection Capabilities
        // RSN Information
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04, // Group Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, // Pairwise Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, // AKM: PSK
        0x00, 0x00, // RSN Capabilities
        // HT Information (802.11n D1.10)
        0x3d, 0x16,
        0x9d, // Primary Channel: 157
        0x0d, // HT Info Subset - secondary channel above, any channel width, RIFS permitted
        0x00, 0x00, 0x00, 0x00, // HT Info Subsets
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Basic MCS Set
        // Overlapping BSS Scan Parameters
        0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
        // Extended Capabilities
        0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
        // VHT Capabilities
        0xbf, 0x0c,
        0xb2, 0x01, 0x80, 0x33, // VHT Capabilities Info
        0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT Supported MCS Set
        // VHT Operation
        0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
        // VHT Tx Power Envelope
        0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
        // Vendor Specific: Atheros Communications, Inc.: Advanced Capability
        0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
        // Vendor Specific: Microsoft Corp.: WMM/WME: Parameter Element
        0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
        0x80, // U-APSD enabled
        0x00,
        0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
        0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
        0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
        0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        // Vendor Specific: Microsoft Corp.: WPS
        0xdd, 0x85, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01,
        0x02, 0x10, 0x3b, 0x00, 0x01, 0x03, 0x10, 0x47, 0x00, 0x10, 0x87, 0x65, 0x43, 0x21, 0x9a,
        0xbc, 0xde, 0xf0, 0x12, 0x34, 0x98, 0xda, 0xc4, 0x8e, 0x86, 0xc8, 0x10, 0x21, 0x00, 0x07,
        0x54, 0x50, 0x2d, 0x4c, 0x69, 0x6e, 0x6b, 0x10, 0x23, 0x00, 0x09, 0x41, 0x72, 0x63, 0x68,
        0x65, 0x72, 0x20, 0x41, 0x37, 0x10, 0x24, 0x00, 0x03, 0x31, 0x2e, 0x30, 0x10, 0x42, 0x00,
        0x0c, 0x41, 0x72, 0x63, 0x68, 0x65, 0x72, 0x20, 0x41, 0x37, 0x20, 0x76, 0x35, 0x10, 0x54,
        0x00, 0x08, 0x00, 0x06, 0x00, 0x50, 0xf2, 0x04, 0x00, 0x01, 0x10, 0x11, 0x00, 0x0a, 0x41,
        0x72, 0x63, 0x68, 0x65, 0x72, 0x41, 0x37, 0x76, 0x35, 0x10, 0x08, 0x00, 0x02, 0x00, 0x04,
        0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20,
    ];

    #[rustfmt::skip]
    const MERGED_IES: &'static [u8] = &[
        // SSID: "foo-ssid"
        0x00, 0x08, 0x66, 0x6f, 0x6f, 0x2d, 0x73, 0x73, 0x69, 0x64,
        // Supported Rates: 6(B), 9, 12(B), 18, 24(B), 36, 48, 54, [Mbit/sec]
        0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
        // DS Parameter set: Current Channel: 157
        0x03, 0x01, 0x9d,
        // Traffic Indication Map (TIM): DTIM 0 of 0 bitmap
        0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
        // Power Constraint: 3
        0x20, 0x01, 0x03,
        // HT Capabilities (802.11n D1.10)
        0x2d, 0x1a,
        0xef, 0x09, // HT Capabilities Info
        0x1b, // A-MPDU Parameters: 0x1b
        0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // MCS Set
        0x00, 0x00, // HT Extended Capabilities
        0x00, 0x00, 0x00, 0x00, // Transmit Beamforming Capabilities
        0x00, // Antenna Selection Capabilities
        // RSN Information
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04, // Group Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, // Pairwise Cipher: AES (CCM)
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, // AKM: PSK
        0x00, 0x00, // RSN Capabilities
        // HT Information (802.11n D1.10)
        0x3d, 0x16,
        0x9d, // Primary Channel: 157
        0x0d, // HT Info Subset - secondary channel above, any channel width, RIFS permitted
        0x00, 0x00, 0x00, 0x00, // HT Info Subsets
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Basic MCS Set
        // Overlapping BSS Scan Parameters
        0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
        // Extended Capabilities
        0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
        // VHT Capabilities
        0xbf, 0x0c,
        0xb2, 0x01, 0x80, 0x33, // VHT Capabilities Info
        0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT Supported MCS Set
        // VHT Operation
        0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
        // VHT Tx Power Envelope
        0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
        // Vendor Specific: Atheros Communications, Inc.: Advanced Capability
        0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
        // Vendor Specific: Microsoft Corp.: WMM/WME: Parameter Element
        0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
        0x80, // U-APSD enabled
        0x00,
        0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
        0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
        0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
        0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        // Vendor Specific: Microsoft Corp.: WPS
        0xdd, 0x85, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01,
        0x02, 0x10, 0x3b, 0x00, 0x01, 0x03, 0x10, 0x47, 0x00, 0x10, 0x87, 0x65, 0x43, 0x21, 0x9a,
        0xbc, 0xde, 0xf0, 0x12, 0x34, 0x98, 0xda, 0xc4, 0x8e, 0x86, 0xc8, 0x10, 0x21, 0x00, 0x07,
        0x54, 0x50, 0x2d, 0x4c, 0x69, 0x6e, 0x6b, 0x10, 0x23, 0x00, 0x09, 0x41, 0x72, 0x63, 0x68,
        0x65, 0x72, 0x20, 0x41, 0x37, 0x10, 0x24, 0x00, 0x03, 0x31, 0x2e, 0x30, 0x10, 0x42, 0x00,
        0x0c, 0x41, 0x72, 0x63, 0x68, 0x65, 0x72, 0x20, 0x41, 0x37, 0x20, 0x76, 0x35, 0x10, 0x54,
        0x00, 0x08, 0x00, 0x06, 0x00, 0x50, 0xf2, 0x04, 0x00, 0x01, 0x10, 0x11, 0x00, 0x0a, 0x41,
        0x72, 0x63, 0x68, 0x65, 0x72, 0x41, 0x37, 0x76, 0x35, 0x10, 0x08, 0x00, 0x02, 0x00, 0x04,
        0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20,
    ];

    #[test]
    fn test_no_merge() {
        let ies = IesMerger::new(BEACON_FRAME_IES.to_vec()).finalize();
        assert_eq!(&ies[..], BEACON_FRAME_IES);
    }

    #[test]
    fn test_merge_same_ies() {
        let mut ies_merger = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger.merge(BEACON_FRAME_IES);
        let ies = ies_merger.finalize();
        assert_eq!(&ies[..], BEACON_FRAME_IES);
    }

    #[test]
    fn test_merge_different_ies() {
        let mut ies_merger = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger.merge(PROBE_RESP_IES);
        let ies = ies_merger.finalize();
        assert_eq!(&ies[..], MERGED_IES)
    }

    // Verify that merging IEs in a different order should still produce the same result
    // (this is not true in the general case, but is true with the data we mock)
    #[test]
    fn test_merge_different_ies_commutative() {
        let mut ies_merger1 = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger1.merge(PROBE_RESP_IES);
        let ies1 = ies_merger1.finalize();

        let mut ies_merger2 = IesMerger::new(PROBE_RESP_IES.to_vec());
        ies_merger2.merge(BEACON_FRAME_IES);
        let ies2 = ies_merger2.finalize();

        assert_eq!(ies1, ies2);
    }

    // In the course of a scan, we may merge the same thing multiple times.
    // This tests such use case.
    #[test]
    fn test_merge_redundant() {
        let mut ies_merger = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger.merge(BEACON_FRAME_IES);
        ies_merger.merge(PROBE_RESP_IES);
        ies_merger.merge(PROBE_RESP_IES);
        ies_merger.merge(BEACON_FRAME_IES);
        let ies = ies_merger.finalize();

        assert_eq!(&ies[..], MERGED_IES);
    }

    // Test that we don't overuse memory if an unfriendly AP repeatedly updates the IEs.
    #[test]
    fn test_merge_is_resilient() {
        const LEN: u8 = 255;
        let mut ies = vec![0xdd, LEN];
        ies.extend(vec![0xff; LEN as usize].iter());

        let mut ies_merger = IesMerger::new(ies.clone());
        for i in 0..255 {
            let last_idx = ies.len() - 1;
            ies[last_idx] = i; // Tweak one value so the IEs is different, forcing a merge.
            ies_merger.merge(&ies[..]);
        }
        // Verify we don't use too much memory.
        assert!(ies_merger.ies_buf.len() <= IES_MERGER_BUFFER_LIMIT);

        // We should still produce a result.
        let result_ies = ies_merger.finalize();
        assert_eq!(ies.len(), result_ies.len());
        // Verify one IE picked (whichever IE is picked, all but the last byte are the same).
        assert_eq!(ies[..ies.len() - 1], result_ies[..result_ies.len() - 1]);
    }

    #[test]
    fn test_merge_prioritize_non_hidden_ssid_1() {
        let hidden_ssid_ie = vec![0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
        let mut ies_merger = IesMerger::new(BEACON_FRAME_IES.to_vec());
        ies_merger.merge(&hidden_ssid_ie[..]);
        let ies = ies_merger.finalize();

        // The non-hidden SSID IE is kept.
        assert_eq!(&ies[..], BEACON_FRAME_IES);

        // Sanity check by initializing with hidden SSID first.
        let mut ies_merger = IesMerger::new(hidden_ssid_ie.clone());
        // This verifies that hidden SSID IE is kept if it's the only one.
        assert_eq!(ies_merger.finalize(), hidden_ssid_ie);

        let mut ies_merger = IesMerger::new(hidden_ssid_ie);
        ies_merger.merge(BEACON_FRAME_IES);
        let ies = ies_merger.finalize();

        // The non-hidden SSID IE is kept.
        assert_eq!(&ies[..], BEACON_FRAME_IES);
    }
}
