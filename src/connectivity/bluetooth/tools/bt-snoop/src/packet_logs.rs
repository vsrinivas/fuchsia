// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    byteorder::{BigEndian, WriteBytesExt},
    fidl_fuchsia_bluetooth_snoop::{PacketType, SnoopPacket as FidlSnoopPacket},
    fuchsia_inspect::{self as inspect, Property},
    itertools::Itertools,
    std::{
        collections::{
            vec_deque::{Iter as VecDequeIter, VecDeque},
            HashMap,
        },
        time::Duration,
    },
};

use crate::{
    bounded_queue::{BoundedQueue, CreatedAt, SizeOf},
    DeviceId,
};

pub struct SnoopPacket {
    inner: FidlSnoopPacket,
    _inspect: inspect::BytesProperty,
}

impl std::ops::Deref for SnoopPacket {
    type Target = FidlSnoopPacket;
    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl std::ops::DerefMut for SnoopPacket {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

// Size for SnoopPacket must be implemented here because SnoopPacket is defined in generated code.
impl SizeOf for SnoopPacket {
    fn size_of(&self) -> usize {
        std::mem::size_of::<Self>() + self.payload.len()
    }
}

// CreatedAt for SnoopPacket must be implemented here because SnoopPacket is defined in generated
// code.
impl CreatedAt for SnoopPacket {
    fn created_at(&self) -> Duration {
        Duration::new(self.timestamp.seconds, self.timestamp.subsec_nanos)
    }
}

/// Alias for a queue of snoop packets.
pub(crate) type PacketLog = BoundedQueue<SnoopPacket>;

/// A container for packet logs for each snoop channel.
// Internal invariant: `device_logs.len()` must always equal `insertion_order.len()`.
// any method that modifies these fields must ensure the invariant holds after the
// method returns.
pub(crate) struct PacketLogs {
    max_device_count: usize,
    log_size_soft_max_bytes: usize,
    log_size_hard_max_bytes: usize,
    log_age: Duration,
    device_logs: HashMap<DeviceId, PacketLog>,
    insertion_order: VecDeque<DeviceId>,

    // Inspect Data
    inspect: inspect::Node,
    logging_for_devices: inspect::StringProperty,
}

impl PacketLogs {
    /// Create a new `PacketLogs` struct. `max_device_count` sets the number of hci devices the
    /// logger will store packets for. `log_size_soft_max_bytes` sets the size limit associated with each
    /// device (see `BoundedQueue` documentation for more information). `log_age` sets the age limit
    /// associated with each device (see `BoundedQueue` documentation for more information).
    ///
    /// Note that the `log_size_soft_max_bytes` and `log_age` values are set on a _per device_ basis.
    ///
    /// Panics if `max_device_count` is 0.
    pub fn new(
        max_device_count: usize,
        log_size_soft_max_bytes: usize,
        log_size_hard_max_bytes: usize,
        log_age: Duration,
        inspect: inspect::Node,
    ) -> PacketLogs {
        assert!(max_device_count != 0, "Cannot create a `PacketLog` with a max_device_count of 0");
        let logging_for_devices = inspect.create_string("logging_active_for_devices", "");
        PacketLogs {
            max_device_count,
            log_size_soft_max_bytes,
            log_size_hard_max_bytes,
            log_age,
            device_logs: HashMap::new(),
            insertion_order: VecDeque::new(),
            inspect,
            logging_for_devices,
        }
    }

    /// Add a log to record packets for a new device. Return the `DeviceId` of the oldest log if it
    /// was removed to make room for the new device log.
    /// If the device is already being recorded, this method does nothing.
    pub fn add_device(&mut self, device: DeviceId) -> Option<DeviceId> {
        if self.device_logs.contains_key(&device) {
            return None;
        }
        // Add log and update insertion order metadata
        self.insertion_order.push_back(device.clone());
        let bounded_queue_metrics = self.inspect.create_child(&format!("device_{}", device));
        self.device_logs.insert(
            device.clone(),
            BoundedQueue::new(
                self.log_size_soft_max_bytes,
                self.log_size_hard_max_bytes,
                self.log_age,
                bounded_queue_metrics,
            ),
        );

        // Remove old log and its insertion order metadata if there are too many logs
        //
        // TODO (belgum): This is a first pass at an algorithm to determine which log to drop in
        // the case of too many logs. Alternatives that can be explored in the future include
        // variations of LRU or LFU caching which may or may not account for empty device logs.
        let removed =
            if self.device_logs.len() > self.max_device_count { self.drop_oldest() } else { None };

        let device_ids = self.device_ids().map(|id| format!("{:?}", id)).join(", ");
        self.logging_for_devices.set(&device_ids);

        removed
    }

    // This method requires that there are logs for at least 1 device.
    fn drop_oldest(&mut self) -> Option<DeviceId> {
        if let Some(oldest_key) = self.insertion_order.pop_front() {
            // remove the bounded queue associated with the oldest DeviceId.
            self.device_logs.remove(&oldest_key);
            Some(oldest_key)
        } else {
            None
        }
    }

    /// Get a mutable reference to a single log by `DeviceId`
    pub fn get_log_mut(&mut self, device: &DeviceId) -> Option<&mut PacketLog> {
        self.device_logs.get_mut(device)
    }

    /// Iterator over the device ids which `PacketLogs` is currently recording packets for.
    pub fn device_ids<'a>(&'a self) -> VecDequeIter<'a, DeviceId> {
        self.insertion_order.iter()
    }

    /// Log a packet for a given device. If the device is not being logged, the packet is
    /// dropped.
    pub fn log_packet(&mut self, device: &DeviceId, packet: FidlSnoopPacket) {
        // If the packet log has been removed, there's not much we can do with the packet.
        if let Some(packet_log) = self.device_logs.get_mut(device) {
            let inspect = packet_log
                .get_inspect()
                .create_bytes(packet_log.get_monotonic_count().to_string(), to_pcap_fmt(&packet));
            let packet = SnoopPacket { inner: packet, _inspect: inspect };
            packet_log.insert(packet);
        }
    }
}

const PCAP_CMD: u8 = 0x01;
const PCAP_DATA: u8 = 0x02;
const PCAP_EVENT: u8 = 0x04;

// Format described in
// https://wiki.wireshark.org/Development/LibpcapFileFormat#Record_.28Packet.29_Header
fn to_pcap_fmt(pkt: &FidlSnoopPacket) -> Vec<u8> {
    let mut wtr = vec![];
    wtr.write_u32::<BigEndian>(pkt.timestamp.seconds as u32).unwrap(); // timestamp seconds
    let microseconds = pkt.timestamp.subsec_nanos / 1_000 as u32; // timestamp microseconds
    wtr.write_u32::<BigEndian>(microseconds).unwrap();
    // length is len(payload) + 4 octets for is_received + 1 octet for packet type
    wtr.write_u32::<BigEndian>((pkt.payload.len() + 5) as u32).unwrap(); // number of octets of packet saved
    wtr.write_u32::<BigEndian>((pkt.original_len + 5) as u32).unwrap(); // actual length of packet
    wtr.write_u32::<BigEndian>(pkt.is_received as u32).unwrap();
    match pkt.type_ {
        PacketType::Cmd => {
            wtr.write_u8(PCAP_CMD).unwrap();
        }
        PacketType::Data => {
            wtr.write_u8(PCAP_DATA).unwrap();
        }
        PacketType::Event => {
            wtr.write_u8(PCAP_EVENT).unwrap();
        }
    }
    wtr.extend(&pkt.payload);
    wtr
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_bluetooth_snoop as fidl;

    #[test]
    fn test_pcap_formatting() {
        // cmd packet
        let pkt = fidl::SnoopPacket {
            is_received: false,
            type_: fidl::PacketType::Cmd,
            timestamp: fidl::Timestamp { seconds: 123, subsec_nanos: 0 },
            original_len: 5,
            payload: vec![0, 1, 2, 3, 4],
        };
        let output = to_pcap_fmt(&pkt);
        let expected =
            vec![0, 0, 0, 123, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 10, 0, 0, 0, 0, 1, 0, 1, 2, 3, 4];
        assert_eq!(output, expected);

        // truncated data packet
        let pkt = fidl::SnoopPacket {
            is_received: false,
            type_: fidl::PacketType::Data,
            timestamp: fidl::Timestamp { seconds: 0, subsec_nanos: 0 },
            original_len: 10,
            payload: vec![0, 1, 2, 3, 4],
        };
        let output = to_pcap_fmt(&pkt);
        let expected =
            vec![0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 15, 0, 0, 0, 0, 2, 0, 1, 2, 3, 4];
        assert_eq!(output, expected);

        // empty event packet
        let pkt = fidl::SnoopPacket {
            is_received: false,
            type_: fidl::PacketType::Event,
            timestamp: fidl::Timestamp { seconds: 0, subsec_nanos: 10u32.pow(9) - 1 },
            original_len: 0,
            payload: vec![],
        };
        let output = to_pcap_fmt(&pkt);
        let expected = vec![0, 0, 0, 0, 0, 15, 66, 63, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 0, 4];
        assert_eq!(output, expected);
    }
}
