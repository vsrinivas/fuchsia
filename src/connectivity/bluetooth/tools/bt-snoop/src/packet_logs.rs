// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    byteorder::{BigEndian, WriteBytesExt},
    fidl_fuchsia_bluetooth_snoop::PacketType,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_zircon as zx,
    futures::{
        future::{BoxFuture, FutureExt},
        lock::Mutex,
    },
    inspect_format::constants::MINIMUM_VMO_SIZE_BYTES,
    itertools::Itertools,
    std::{
        collections::{
            vec_deque::{Iter as VecDequeIter, VecDeque},
            HashMap,
        },
        io::Write,
        sync::Arc,
        time::Duration,
    },
    tracing::warn,
};

use crate::{
    bounded_queue::BoundedQueue, clock::utc_clock_transformation, snooper::SnoopPacket, DeviceId,
};

fn generate_lazy_values_for_packet_log(
    device_name: String,
    packet_log: Arc<Mutex<PacketLog>>,
) -> BoxFuture<'static, Result<inspect::Inspector, Error>> {
    async move {
        let mut guard = packet_log.lock().await;
        let utc_xform = utc_clock_transformation();

        let log: &mut PacketLog = &mut *guard;
        let byte_len = log.byte_len();
        let number_of_items = log.len();
        let mut data = Vec::with_capacity(byte_len);
        write_pcap_header(&mut data)?;
        for pkt in log.iter_mut() {
            append_pcap(&mut data, &pkt, utc_xform.as_ref())?;
        }
        drop(log);
        drop(guard);

        let vmo_size = data.len() + MINIMUM_VMO_SIZE_BYTES;
        let inspector = inspect::Inspector::new_with_size(vmo_size);
        let root = inspector.root();
        root.record_string("hci_device_name", device_name);
        root.record_uint("byte_len", byte_len as u64);
        root.record_uint("number_of_items", number_of_items as u64);
        root.record_bytes("data", &data);
        Ok(inspector)
    }
    .boxed()
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
    device_logs: HashMap<DeviceId, (Arc<Mutex<PacketLog>>, inspect::Node)>,
    insertion_order: VecDeque<DeviceId>,

    // Inspect Data
    inspect: inspect::Node,
    logged_devices_inspect_str: inspect::StringProperty,
}

impl PacketLogs {
    /// Create a new `PacketLogs` struct. `max_device_count` sets the number of hci devices the
    /// logger will store packets for. `log_size_soft_max_bytes` sets the size limit associated with
    /// each device (see `BoundedQueue` documentation for more information). `log_age` sets the age
    /// limit associated with each device (see `BoundedQueue` documentation for more information).
    ///
    /// Note that the `log_size_soft_max_bytes` and `log_age` values are set on a _per device_
    /// basis.
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
        let logged_devices_inspect_str = inspect.create_string("logging_active_for_devices", "");
        PacketLogs {
            max_device_count,
            log_size_soft_max_bytes,
            log_size_hard_max_bytes,
            log_age,
            device_logs: HashMap::new(),
            insertion_order: VecDeque::new(),
            inspect,
            logged_devices_inspect_str,
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
        let bounded_queue = Arc::new(Mutex::new(BoundedQueue::new(
            self.log_size_soft_max_bytes,
            self.log_size_hard_max_bytes,
            self.log_age,
        )));
        let bounded_queue_metrics = self.inspect.create_child(inspect::unique_name("device_"));

        bounded_queue_metrics.record_lazy_values("snoop_values", {
            let d = device.to_string();
            let q = bounded_queue.clone();
            move || generate_lazy_values_for_packet_log(d.clone(), q.clone())
        });

        let _ = self.device_logs.insert(device, (bounded_queue, bounded_queue_metrics));

        // Remove old log and its insertion order metadata if there are too many logs
        //
        // This is a first pass at an algorithm to determine which log to drop in
        // the case of too many logs. Alternatives that can be explored in the future include
        // variations of LRU or LFU caching which may or may not account for empty device logs.
        let removed =
            if self.device_logs.len() > self.max_device_count { self.drop_oldest() } else { None };

        let device_ids = self.device_ids().map(|id| format!("{:?}", id)).join(", ");
        self.logged_devices_inspect_str.set(&device_ids);

        removed
    }

    // This method requires that there are logs for at least 1 device.
    fn drop_oldest(&mut self) -> Option<DeviceId> {
        if let Some(oldest_key) = self.insertion_order.pop_front() {
            // remove the bounded queue associated with the oldest DeviceId.
            if self.device_logs.remove(&oldest_key).is_none() {
                warn!("device log for {} was missing on drop", oldest_key);
            }
            Some(oldest_key)
        } else {
            None
        }
    }

    /// Get a shared reference to a single log by `DeviceId`. Returns `None` if there is no log for
    /// a device with the provided id.
    pub fn get(&self, device: &DeviceId) -> Option<Arc<Mutex<PacketLog>>> {
        self.device_logs.get(device).map(|d| d.0.clone())
    }

    /// Iterator over the device ids which `PacketLogs` is currently recording packets for.
    pub fn device_ids<'a>(&'a self) -> VecDequeIter<'a, DeviceId> {
        self.insertion_order.iter()
    }

    /// Log a packet for a given device. If the device is not being logged, the packet is
    /// dropped.
    pub async fn log_packet(&mut self, device: &DeviceId, packet: SnoopPacket) {
        // If the packet log has been removed, there's not much we can do with the packet.
        if let Some(packet_log) = self.device_logs.get_mut(device) {
            packet_log.0.lock().await.insert(packet);
        }
    }
}

const PCAP_CMD: u8 = 0x01;
const PCAP_DATA: u8 = 0x02;
const PCAP_EVENT: u8 = 0x04;

// Format described in https://wiki.wireshark.org/Development/LibpcapFileFormat#Global_Header
pub(crate) fn write_pcap_header<W: Write>(mut buffer: W) -> Result<(), Error> {
    buffer.write_u32::<BigEndian>(0xa1b2c3d4)?; // Magic number
    buffer.write_u16::<BigEndian>(2)?; // Major Version
    buffer.write_u16::<BigEndian>(4)?; // Minor Version
    buffer.write_i32::<BigEndian>(0)?; // Timezone: GMT
    buffer.write_u32::<BigEndian>(0)?; // Sigfigs
    buffer.write_u32::<BigEndian>(65535)?; // Max packet length
    buffer.write_u32::<BigEndian>(201)?; // Protocol: BLUETOOTH_HCI_H4_WITH_PHDR
    Ok(())
}

// Format described in
// https://wiki.wireshark.org/Development/LibpcapFileFormat#Record_.28Packet.29_Header
pub(crate) fn append_pcap<W: Write>(
    mut buffer: W,
    pkt: &SnoopPacket,
    clock_xform: Option<&zx::ClockTransformation>,
) -> Result<(), Error> {
    let (seconds, nanos) = pkt.timestamp_parts(clock_xform);
    // timestamp seconds
    buffer.write_u32::<BigEndian>(seconds as u32)?;
    // timestamp microseconds
    let microseconds = (nanos / 1_000) as u32;
    buffer.write_u32::<BigEndian>(microseconds)?;
    // number of octets of packet saved
    // length is len(payload) + 4 octets for is_received + 1 octet for packet type
    buffer.write_u32::<BigEndian>((pkt.payload.len() + 5) as u32)?;
    // actual length of packet
    buffer.write_u32::<BigEndian>((pkt.original_len + 5) as u32)?;
    buffer.write_u32::<BigEndian>(pkt.is_received as u32)?;
    match pkt.type_ {
        PacketType::Cmd => buffer.write_u8(PCAP_CMD)?,
        PacketType::Data => buffer.write_u8(PCAP_DATA)?,
        PacketType::Event => buffer.write_u8(PCAP_EVENT)?,
    }
    let written = buffer.write(&pkt.payload)?;
    if written != pkt.payload.len() {
        warn!("Couldn't write entire payload, only wrote {} < {}", written, pkt.payload.len());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, fuchsia_inspect::assert_data_tree};

    /// An empty log tests that the most basic inspect data is plumbed through the lazy generation
    /// function. See top level tests module for more integrated tests.
    #[fasync::run_until_stalled(test)]
    async fn test_generated_inspect_values_for_empty_log() {
        let log = Arc::new(Mutex::new(PacketLog::new(
            3 * std::mem::size_of::<SnoopPacket>(),
            3 * std::mem::size_of::<SnoopPacket>(),
            Duration::new(0, 0),
        )));

        let id = "001".to_string();

        let mut hdr = vec![];
        write_pcap_header(&mut hdr).expect("write to succeed");

        let inspect = generate_lazy_values_for_packet_log(id.clone(), log.clone()).await.unwrap();
        assert_data_tree!(inspect, root: {
            hci_device_name: id,
            byte_len: 0u64,
            number_of_items: 0u64,
            data: hdr,
        });
    }

    #[test]
    fn test_pcap_formatting() {
        // cmd packet
        let ts = zx::Time::from_nanos(123 * 1_000_000_000);
        let pkt = SnoopPacket::new(false, PacketType::Cmd, ts, vec![0, 1, 2, 3, 4]);
        let mut output = vec![];
        append_pcap(&mut output, &pkt, None).expect("write to succeed");
        let expected =
            vec![0, 0, 0, 123, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 10, 0, 0, 0, 0, 1, 0, 1, 2, 3, 4];
        assert_eq!(output, expected);

        // truncated data packet
        let ts = zx::Time::from_nanos(0);
        let mut pkt = SnoopPacket::new(false, PacketType::Data, ts, vec![0, 1, 2, 3, 4]);
        pkt.original_len = 10;
        let mut output = vec![];
        append_pcap(&mut output, &pkt, None).expect("write to succeed");
        let expected =
            vec![0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 15, 0, 0, 0, 0, 2, 0, 1, 2, 3, 4];
        assert_eq!(output, expected);

        // empty event packet
        let ts = zx::Time::from_nanos(10i64.pow(9) - 1);
        let pkt = SnoopPacket::new(false, PacketType::Event, ts, vec![]);
        let mut output = vec![];
        append_pcap(&mut output, &pkt, None).expect("write to succeed");
        let expected = vec![0, 0, 0, 0, 0, 15, 66, 63, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 0, 4];
        assert_eq!(output, expected);
    }
}
