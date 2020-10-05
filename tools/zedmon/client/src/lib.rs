// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::protocol::{self, ParameterValue, ReportFormat, Value, MAX_PACKET_SIZE},
    anyhow::{bail, Error},
    std::{
        cell::RefCell,
        collections::HashMap,
        io::{Read, Write},
        os::raw::{c_uchar, c_ushort},
    },
    usb_bulk::InterfaceInfo,
};

const GOOGLE_VENDOR_ID: c_ushort = 0x18d1;
const ZEDMON_PRODUCT_ID: c_ushort = 0xaf00;
const VENDOR_SPECIFIC_CLASS_ID: c_uchar = 0xff;
const ZEDMON_SUBCLASS_ID: c_uchar = 0xff;
const ZEDMON_PROTOCOL_ID: c_uchar = 0x00;

/// Matches the USB interface info of a Zedmon device.
fn zedmon_match(ifc: &InterfaceInfo) -> bool {
    (ifc.dev_vendor == GOOGLE_VENDOR_ID)
        && (ifc.dev_product == ZEDMON_PRODUCT_ID)
        && (ifc.ifc_class == VENDOR_SPECIFIC_CLASS_ID)
        && (ifc.ifc_subclass == ZEDMON_SUBCLASS_ID)
        && (ifc.ifc_protocol == ZEDMON_PROTOCOL_ID)
}

/// Used by ZedmonClient to determine when data reporting should stop.
pub trait StopSignal {
    fn should_stop(&mut self) -> Result<bool, Error>;
}

/// Writes a Report to a CSV-formatted line of output.
fn report_to_string(
    report: protocol::Report,
    shunt_resistance: f32,
    v_shunt_index: usize,
    v_bus_index: usize,
) -> String {
    let v_shunt = report.values[v_shunt_index];
    let v_bus = report.values[v_bus_index];
    let (v_shunt, v_bus) = match (v_shunt, v_bus) {
        (Value::F32(x), Value::F32(y)) => (x, y),
        t => panic!("Got wrong value types for (v_shunt, v_bus): {:?}", t),
    };

    let power = v_bus * v_shunt / shunt_resistance;
    format!("{},{},{},{}\n", report.timestamp, v_shunt, v_bus, power)
}

/// Interface to a Zedmon device.
#[derive(Debug)]
pub struct ZedmonClient<InterfaceType>
where
    InterfaceType: usb_bulk::Open<InterfaceType> + Read + Write,
{
    /// USB interface to the Zedmon device, or equivalent.
    interface: RefCell<InterfaceType>,

    /// Format of each field in a Report.
    field_formats: Vec<ReportFormat>,

    /// Information necessary to obtain power data from direct Zedmon measurements.
    shunt_resistance: f32,
    v_shunt_index: usize,
    v_bus_index: usize,
}

impl<InterfaceType: usb_bulk::Open<InterfaceType> + Read + Write> ZedmonClient<InterfaceType> {
    /// Enumerates all connected Zedmons. Returns a `Vec<String>` of their serial numbers.
    fn enumerate() -> Vec<String> {
        let mut serials = Vec::new();

        // Instead of matching any devices, this callback extracts Zedmon serial numbers as
        // InterfaceType::open iterates through them. InterfaceType::open is expected to return an
        // error because no devices match.
        let mut cb = |info: &InterfaceInfo| -> bool {
            if zedmon_match(info) {
                let null_pos = match info.serial_number.iter().position(|&c| c == 0) {
                    Some(p) => p,
                    None => {
                        eprintln!("Warning: Detected a USB device whose serial number was not null-terminated:");
                        eprintln!(
                            "{}",
                            (*String::from_utf8_lossy(&info.serial_number)).to_string()
                        );
                        return false;
                    }
                };
                serials
                    .push((*String::from_utf8_lossy(&info.serial_number[..null_pos])).to_string());
            }
            false
        };

        assert!(
            InterfaceType::open(&mut cb).is_err(),
            "open() should return an error, as the supplied callback cannot match any devices."
        );
        serials
    }

    /// Creates a new ZedmonClient instance.
    // TODO(fxbug.dev/61148): Make the behavior predictable if multiple Zedmons are attached.
    fn new() -> Result<Self, Error> {
        let mut interface = InterfaceType::open(&mut zedmon_match).unwrap();
        let parameters = Self::get_parameters(&mut interface).unwrap();
        let field_formats = Self::get_field_formats(&mut interface).unwrap();

        let shunt_resistance = {
            let value = parameters["shunt_resistance"];
            if let Value::F32(v) = value {
                v
            } else {
                bail!("Wrong value type for shunt_resistance: {:?}", value);
            }
        };

        // Use a HashMap to assist in field lookup for simplicity. Note that the Vec<ReportFormat>
        // representation needs to be retained for later Report-parsing.
        let formats_by_name: HashMap<String, ReportFormat> =
            field_formats.iter().map(|f| (f.name.clone(), f.clone())).collect();
        let v_shunt_index = formats_by_name["v_shunt"].index as usize;
        let v_bus_index = formats_by_name["v_bus"].index as usize;

        Ok(Self {
            interface: RefCell::new(interface),
            field_formats,
            shunt_resistance,
            v_shunt_index,
            v_bus_index,
        })
    }

    /// Retrieves a ParameterValue from the provided Zedmon interface.
    fn get_parameter(interface: &mut InterfaceType, index: u8) -> Result<ParameterValue, Error> {
        let request = protocol::encode_query_parameter(index);
        interface.write(&request)?;

        let mut response = [0; MAX_PACKET_SIZE];
        let len = interface.read(&mut response)?;

        Ok(protocol::parse_parameter_value(&mut &response[0..len])?)
    }

    /// Retrieves every ParameterValue from the provided Zedmon interface.
    fn get_parameters(interface: &mut InterfaceType) -> Result<HashMap<String, Value>, Error> {
        let mut parameters = HashMap::new();
        loop {
            let parameter = Self::get_parameter(interface, parameters.len() as u8)?;
            if parameter.name.is_empty() {
                return Ok(parameters);
            }
            parameters.insert(parameter.name, parameter.value);
        }
    }

    /// Retrieves a ReportFormat from the provided Zedmon interface.
    fn get_report_format(interface: &mut InterfaceType, index: u8) -> Result<ReportFormat, Error> {
        let request = protocol::encode_query_report_format(index);
        interface.write(&request)?;

        let mut response = [0; MAX_PACKET_SIZE];
        let len = interface.read(&mut response)?;

        Ok(protocol::parse_report_format(&response[..len])?)
    }

    /// Retrieves the ReportFormat for each Report field from the provided Zedmon interface.
    fn get_field_formats(interface: &mut InterfaceType) -> Result<Vec<ReportFormat>, Error> {
        let mut all_fields = vec![];
        loop {
            let format = Self::get_report_format(interface, all_fields.len() as u8)?;
            if format.index == protocol::REPORT_FORMAT_INDEX_END {
                return Ok(all_fields);
            }
            all_fields.push(format);
        }
    }

    /// Disables reporting on the Zedmon device.
    fn disable_reporting(&self) -> Result<(), Error> {
        let request = protocol::encode_disable_reporting();
        self.interface.borrow_mut().write(&request)?;
        Ok(())
    }

    /// Enables reporting on the Zedmon device.
    fn enable_reporting(&self) -> Result<(), Error> {
        let request = protocol::encode_enable_reporting();
        self.interface.borrow_mut().write(&request)?;
        Ok(())
    }

    /// Reads reported data from the Zedmon device, taking care of enabling/disabling reporting.
    ///
    /// Measurement data is written to `writer`. Reporting will cease when `stopper` raises its stop
    /// signal.
    pub fn read_reports(
        &self,
        mut writer: Box<dyn Write + Send>,
        mut stopper: impl StopSignal,
    ) -> Result<(), Error> {
        // This function's workload is shared between its main thread and `output_thread`.
        //
        // The main thread enables reporting, reads USB packets via blocking reads, and sends those
        // packets to `output_thread` via `packet_sender`. When `stopper` indicates that reporting
        // should stop, it drops `packet_sender` to close the channel. `output_thread` will still
        // receive packets that have been sent before closure.
        //
        // Meanwhile, `output_thread` parses each packet it receives to a Vec<Report>, which it then
        // formats and outputs via `writer`.
        //
        // The multithreading has not been confirmed as necessary for performance reasons, but it
        // seems like a reasonable thing to do, as both reading from USB and outputting (typically
        // to stdout or a file) involve blocking on I/O.
        let (packet_sender, packet_receiver) = std::sync::mpsc::channel::<Vec<u8>>();

        // Prepare data to move into `output_thread`.
        let parser = protocol::ReportParser::new(&self.field_formats)?;
        let shunt_resistance = self.shunt_resistance;
        let v_shunt_index = self.v_shunt_index;
        let v_bus_index = self.v_bus_index;

        let output_thread = std::thread::spawn(move || -> Result<(), Error> {
            for buffer in packet_receiver.iter() {
                let reports = parser.parse_reports(&buffer).unwrap();
                for report in reports.into_iter() {
                    write!(
                        *writer,
                        "{}",
                        report_to_string(report, shunt_resistance, v_shunt_index, v_bus_index,)
                    )?;
                }
            }
            writer.flush()?;
            Ok(())
        });

        // Enable reporting and run the main loop.
        self.enable_reporting()?;
        loop {
            let mut buffer = vec![0; MAX_PACKET_SIZE];
            match self.interface.borrow_mut().read(&mut buffer) {
                Err(e) => eprintln!("USB read error: {}", e),
                Ok(bytes_read) => {
                    buffer.truncate(bytes_read);
                    packet_sender.send(buffer).unwrap();
                }
            }

            if stopper.should_stop()? {
                self.disable_reporting()?;
                drop(packet_sender);
                break;
            }
        }

        // Wait for the parsing thread to complete upon draining the channel buffer.
        output_thread.join().unwrap().unwrap();
        Ok(())
    }
}

/// Lists the serial numbers of all connected Zedmons.
pub fn list() -> Vec<String> {
    ZedmonClient::<usb_bulk::Interface>::enumerate()
}

pub fn zedmon() -> ZedmonClient<usb_bulk::Interface> {
    let result = ZedmonClient::<usb_bulk::Interface>::new();
    if result.is_err() {
        eprintln!("Error initializing ZedmonClient: {:?}", result);
    }
    result.unwrap()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{format_err, Error},
        lazy_static::lazy_static,
        protocol::{Report, ScalarType},
        std::collections::VecDeque,
        std::sync::{
            atomic::{AtomicBool, Ordering},
            mpsc, Arc, RwLock,
        },
        std::time::Duration,
        test_util::assert_near,
    };

    // Used by `interface_info`, below, as a convenient means of constructing InterfaceInfo.
    struct ShortInterface<'a> {
        dev_vendor: ::std::os::raw::c_ushort,
        dev_product: ::std::os::raw::c_ushort,
        ifc_class: ::std::os::raw::c_uchar,
        ifc_subclass: ::std::os::raw::c_uchar,
        ifc_protocol: ::std::os::raw::c_uchar,
        serial_number: &'a str,
    }

    fn interface_info(short: ShortInterface<'_>) -> InterfaceInfo {
        let mut serial = [0; 256];
        for (i, c) in short.serial_number.as_bytes().iter().enumerate() {
            serial[i] = *c;
        }

        InterfaceInfo {
            dev_vendor: short.dev_vendor,
            dev_product: short.dev_product,
            dev_class: 0,
            dev_subclass: 0,
            dev_protocol: 0,
            ifc_class: short.ifc_class,
            ifc_subclass: short.ifc_subclass,
            ifc_protocol: short.ifc_protocol,
            has_bulk_in: 0,
            has_bulk_out: 0,
            writable: 0,
            serial_number: serial,
            device_path: [0; 256usize],
        }
    }

    #[test]
    fn test_enumerate() {
        // AVAILABLE_DEVICES is state for the static method FakeEnumerationInterface::open. This
        // test is single-threaded, so a thread-local static provides the most appropriate safe
        // interface.
        thread_local! {
            static AVAILABLE_DEVICES: RefCell<Vec<InterfaceInfo>> = RefCell::new(Vec::new());
        }
        fn push_device(short: ShortInterface<'_>) {
            AVAILABLE_DEVICES.with(|devices| {
                devices.borrow_mut().push(interface_info(short));
            });
        }

        struct FakeEnumerationInterface {}

        impl usb_bulk::Open<FakeEnumerationInterface> for FakeEnumerationInterface {
            fn open<F>(matcher: &mut F) -> Result<FakeEnumerationInterface, Error>
            where
                F: FnMut(&InterfaceInfo) -> bool,
            {
                AVAILABLE_DEVICES.with(|devices| {
                    let devices = devices.borrow();
                    for device in devices.iter() {
                        if matcher(device) {
                            return Ok(FakeEnumerationInterface {});
                        }
                    }
                    Err(format_err!("No matching devices found."))
                })
            }
        }

        impl Read for FakeEnumerationInterface {
            fn read(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
                Ok(0)
            }
        }

        impl Write for FakeEnumerationInterface {
            fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
                Ok(0)
            }
            fn flush(&mut self) -> std::io::Result<()> {
                Ok(())
            }
        }

        // No devices connected
        let serials = ZedmonClient::<FakeEnumerationInterface>::enumerate();
        assert!(serials.is_empty());

        // One device: not-a-zedmon-1
        push_device(ShortInterface {
            dev_vendor: 0xdead,
            dev_product: ZEDMON_PRODUCT_ID,
            ifc_class: VENDOR_SPECIFIC_CLASS_ID,
            ifc_subclass: ZEDMON_SUBCLASS_ID,
            ifc_protocol: ZEDMON_PROTOCOL_ID,
            serial_number: "not-a-zedmon-1",
        });
        let serials = ZedmonClient::<FakeEnumerationInterface>::enumerate();
        assert!(serials.is_empty());

        // Two devices: not-a-zedmon-1, zedmon-1
        push_device(ShortInterface {
            dev_vendor: GOOGLE_VENDOR_ID,
            dev_product: ZEDMON_PRODUCT_ID,
            ifc_class: VENDOR_SPECIFIC_CLASS_ID,
            ifc_subclass: ZEDMON_SUBCLASS_ID,
            ifc_protocol: ZEDMON_PROTOCOL_ID,
            serial_number: "zedmon-1",
        });
        let serials = ZedmonClient::<FakeEnumerationInterface>::enumerate();
        assert_eq!(serials, ["zedmon-1"]);

        // Three devices: not-a-zedmon-1, zedmon-1, not-a-zedmon-2
        push_device(ShortInterface {
            dev_vendor: GOOGLE_VENDOR_ID,
            dev_product: 0xbeef,
            ifc_class: VENDOR_SPECIFIC_CLASS_ID,
            ifc_subclass: ZEDMON_SUBCLASS_ID,
            ifc_protocol: ZEDMON_PROTOCOL_ID,
            serial_number: "not-a-zedmon-2",
        });
        let serials = ZedmonClient::<FakeEnumerationInterface>::enumerate();
        assert_eq!(serials, ["zedmon-1"]);

        // Four devices: not-a-zedmon-1, zedmon-1, not-a-zedmon-2, zedmon-2
        push_device(ShortInterface {
            dev_vendor: GOOGLE_VENDOR_ID,
            dev_product: ZEDMON_PRODUCT_ID,
            ifc_class: VENDOR_SPECIFIC_CLASS_ID,
            ifc_subclass: ZEDMON_SUBCLASS_ID,
            ifc_protocol: ZEDMON_PROTOCOL_ID,
            serial_number: "zedmon-2",
        });
        let serials = ZedmonClient::<FakeEnumerationInterface>::enumerate();
        assert_eq!(serials, ["zedmon-1", "zedmon-2"]);
    }

    // Provides test support for ZedmonClient functionality that interacts with a Zedmon device.
    //
    // FakeZedmonInterface provides test support at the USB interface level. However, a test does
    // not have direct access to ZedmonClient's FakeZedmonInterface instance.
    //
    // This module allows communication between the test and FakeZedmonInterface through a static
    // instance of the Coordinator struct. For reporting, the process is as follows:
    //  - The test calls `init` to populate the static COORDINATOR, passing in a DeviceConfiguration
    //    and a queue of Vec<Report> that FakeZedmonInterface will stream to zedmon. The test
    //    receives a CoordinatorHandle that will destroy COORDINATOR when it goes out of scope.
    //  - The test calls ZedmonClient::read_reports with a stop signal provided by the
    //    CoorinatorHandle.
    //  - Each time Zedmon reads a report packet, FakeZedmonInterface will provide it with a
    //    serialized Vec<Report> from the COORDINATOR's queue.
    //  - When the queue is exhausted, COORDINATOR triggers the stop signal, ending reporting.
    mod fake_device {
        use {
            super::*,
            num::FromPrimitive,
            protocol::{tests::*, PacketType, Unit},
        };

        // StopSignal implementer for testing ZedmonClient::read_reports. The state is set by
        // COORDINATOR.
        pub struct Stopper {
            signal: Arc<AtomicBool>,
        }

        impl StopSignal for Stopper {
            fn should_stop(&mut self) -> Result<bool, Error> {
                return Ok(self.signal.load(Ordering::SeqCst));
            }
        }

        // Coordinates interactions between FakeZedmonInterface and a test.
        //
        // There is meant to be only one instance of this struct, the static COORDINATOR. Within
        // this module, COORDINATOR is accessed by a number of helper functions. Outside of this
        // module (i.e. in a test), COORDINATOR is accessed in RAII fashion via a CoordinatorHandle.
        struct Coordinator {
            device_config: DeviceConfiguration,
            report_queue: VecDeque<Vec<Report>>,
            stop_signal: Arc<AtomicBool>,
        }

        // Constants that are inherent to a Zedmon device.
        #[derive(Clone, Debug)]
        pub struct DeviceConfiguration {
            pub shunt_resistance: f32,
            pub v_shunt_scale: f32,
            pub v_bus_scale: f32,
        }

        // Used to provide tests with RAII access to COORDINATOR.
        pub struct CoordinatorHandle {}

        impl CoordinatorHandle {
            pub fn get_stopper(&self) -> Stopper {
                let lock = COORDINATOR.read().unwrap();
                let coordinator = lock.as_ref().unwrap();
                Stopper { signal: coordinator.stop_signal.clone() }
            }
        }

        impl Drop for CoordinatorHandle {
            fn drop(&mut self) {
                let mut lock = COORDINATOR.write().unwrap();
                lock.take();
            }
        }

        // At time of writing, COORDINATOR is only accessed on the same thread on which it was
        // created, so it could be made thread-local. However, if ZedmonClient were to perform USB
        // reading on a child thread and Report-parsing on its main thread, that would no longer be
        // the case. So for robustness, COORDINATOR is not thread-local.
        lazy_static! {
            static ref COORDINATOR: RwLock<Option<Coordinator>> = RwLock::new(None);
        }

        // Entry point for tests. Populates COORDINATOR and returns a CoordinatorHandle used to
        // access it.
        pub fn init(
            device_config: DeviceConfiguration,
            report_queue: VecDeque<Vec<Report>>,
        ) -> CoordinatorHandle {
            let stop_signal = Arc::new(AtomicBool::new(false));
            let mut lock = COORDINATOR.write().unwrap();

            assert!(
                lock.is_none(),
                "COORDINATOR was not properly cleared; this should happen automatically."
            );

            lock.replace(Coordinator { device_config, report_queue, stop_signal });
            CoordinatorHandle {}
        }

        // Gets COORDINATOR's device_config.
        fn device_config() -> DeviceConfiguration {
            let lock = COORDINATOR.read().unwrap();
            let coordinator = lock.as_ref().unwrap();
            coordinator.device_config.clone()
        }

        // Gets the next packet's worth of Reports from COORDINATOR.
        fn get_reports_for_packet() -> Vec<Report> {
            let mut lock = COORDINATOR.write().unwrap();
            let coordinator = lock.as_mut().unwrap();
            assert!(coordinator.report_queue.len() > 0, "No reports left in queue");
            if coordinator.report_queue.len() == 1 {
                coordinator.stop_signal.store(true, Ordering::SeqCst);
            }
            coordinator.report_queue.pop_front().unwrap()
        }
        // Indicates the contents of the next read from FakeZedmonInterface.
        enum NextRead {
            ParameterValue(u8),
            ReportFormat(u8),
            Report,
        }

        // Interface that provides fakes for testing interactions with a Zedmon device.
        pub struct FakeZedmonInterface {
            // The type of read that wil be performed next from this interface, if any.
            next_read: Option<NextRead>,
        }

        impl usb_bulk::Open<FakeZedmonInterface> for FakeZedmonInterface {
            fn open<F>(_matcher: &mut F) -> Result<FakeZedmonInterface, Error>
            where
                F: FnMut(&InterfaceInfo) -> bool,
            {
                Ok(FakeZedmonInterface { next_read: None })
            }
        }

        impl FakeZedmonInterface {
            // Populates a ParameterValue packet.
            fn read_parameter_value(&mut self, index: u8, buffer: &mut [u8]) -> usize {
                match index {
                    0 => serialize_parameter_value(
                        ParameterValue {
                            name: "shunt_resistance".to_string(),
                            value: Value::F32(device_config().shunt_resistance),
                        },
                        buffer,
                    ),
                    1 => serialize_parameter_value(
                        ParameterValue { name: "".to_string(), value: Value::U8(0) },
                        buffer,
                    ),
                    _ => panic!("Should only receive 0 or 1 as indices"),
                }
            }

            // Populates a ReportFormat packet.
            fn read_report_format(&self, index: u8, buffer: &mut [u8]) -> usize {
                match index {
                    0 => serialize_report_format(
                        ReportFormat {
                            index,
                            field_type: ScalarType::I16,
                            unit: Unit::Volts,
                            scale: device_config().v_shunt_scale,
                            name: "v_shunt".to_string(),
                        },
                        buffer,
                    ),
                    1 => serialize_report_format(
                        ReportFormat {
                            index,
                            field_type: ScalarType::I16,
                            unit: Unit::Volts,
                            scale: device_config().v_bus_scale,
                            name: "v_bus".to_string(),
                        },
                        buffer,
                    ),
                    2 => serialize_report_format(
                        ReportFormat {
                            index: protocol::REPORT_FORMAT_INDEX_END,
                            field_type: ScalarType::U8,
                            unit: Unit::Volts,
                            scale: 0.0,
                            name: "".to_string(),
                        },
                        buffer,
                    ),
                    _ => panic!("Should only receive 0, 1, or 2 as indices"),
                }
            }

            // Populates a Report packet.
            fn read_reports(&mut self, buffer: &mut [u8]) -> usize {
                let reports = get_reports_for_packet();
                serialize_reports(&reports, buffer)
            }
        }

        impl Read for FakeZedmonInterface {
            fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
                let bytes_read = match self.next_read.take().unwrap() {
                    NextRead::ParameterValue(index) => self.read_parameter_value(index, buffer),
                    NextRead::ReportFormat(index) => self.read_report_format(index, buffer),
                    NextRead::Report => {
                        self.next_read = Some(NextRead::Report);
                        self.read_reports(buffer)
                    }
                };
                Ok(bytes_read)
            }
        }

        impl Write for FakeZedmonInterface {
            fn write(&mut self, data: &[u8]) -> std::io::Result<usize> {
                let packet_type = PacketType::from_u8(data[0]).unwrap();
                match packet_type {
                    PacketType::EnableReporting => self.next_read = Some(NextRead::Report),
                    PacketType::DisableReporting => self.next_read = None,
                    PacketType::QueryParameter => {
                        self.next_read = Some(NextRead::ParameterValue(data[1]))
                    }
                    PacketType::QueryReportFormat => {
                        self.next_read = Some(NextRead::ReportFormat(data[1]))
                    }
                    _ => panic!("Not a valid host-to-target packet"),
                }
                Ok(data.len())
            }
            fn flush(&mut self) -> std::io::Result<()> {
                Ok(())
            }
        }
    }

    // Helper struct for testing ZedmonClient::record.
    struct ZedmonRecordRunner {
        // Maps time in nanoseconds to (v_shunt, v_bus).
        voltage_function: Box<dyn Fn(u64) -> (f32, f32)>,

        shunt_resistance: f32,
        v_shunt_scale: f32,
        v_bus_scale: f32,

        // The length of the test, and interval between report timestamps. The first report is sent
        // one interval after the starting instant.
        test_duration: Duration,
        reporting_interval: Duration,
    }

    impl ZedmonRecordRunner {
        fn run(&self) -> Result<String, Error> {
            let device_config = fake_device::DeviceConfiguration {
                shunt_resistance: self.shunt_resistance,
                v_shunt_scale: self.v_shunt_scale,
                v_bus_scale: self.v_bus_scale,
            };

            let mut report_queue = VecDeque::new();

            let mut elapsed = Duration::from_millis(0);

            // 1ms of fake time elapses between each report. Reports are batched into groups of 5,
            // the number that will fit into a single packet.
            while elapsed <= self.test_duration {
                let mut reports = Vec::new();
                for _ in 0..5 {
                    elapsed = elapsed + self.reporting_interval;
                    if elapsed > self.test_duration {
                        break;
                    }

                    let (v_shunt, v_bus) = (self.voltage_function)(elapsed.as_nanos() as u64);
                    reports.push(Report {
                        timestamp: elapsed.as_nanos() as u64,
                        values: vec![
                            Value::I16((v_shunt / self.v_shunt_scale) as i16),
                            Value::I16((v_bus / self.v_bus_scale) as i16),
                        ],
                    });
                }
                if !reports.is_empty() {
                    report_queue.push_back(reports);
                }
            }

            let coordinator = fake_device::init(device_config, report_queue);
            let zedmon = ZedmonClient::<fake_device::FakeZedmonInterface>::new()?;

            // Implements Write by sending bytes over a channel. The holder of the channel's
            // Receiver can then inspect the data that was written to test expectations.
            struct ChannelWriter {
                sender: mpsc::Sender<Vec<u8>>,
                buffer: Vec<u8>,
            }

            impl std::io::Write for ChannelWriter {
                fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
                    self.buffer.extend_from_slice(buf);
                    Ok(buf.len())
                }

                fn flush(&mut self) -> std::io::Result<()> {
                    let mut payload = Vec::new();
                    std::mem::swap(&mut self.buffer, &mut payload);
                    self.sender.send(payload).unwrap();
                    Ok(())
                }
            }

            let (sender, receiver) = mpsc::channel();
            let writer = Box::new(ChannelWriter { sender, buffer: Vec::new() });
            zedmon.read_reports(writer, coordinator.get_stopper())?;

            let output = receiver.recv()?;
            Ok(String::from_utf8(output)?)
        }
    }

    #[test]
    fn test_read_reports() -> Result<(), Error> {
        // The voltages used are simply time-dependent signals that, combined with shunt_resistance
        // below, yield power on the order of a few Watts.
        fn get_voltages(nanos: u64) -> (f32, f32) {
            let seconds = nanos as f32 / 1e9;
            let v_shunt = 1e-3 + 2e-4 * (std::f32::consts::PI * seconds).cos();
            let v_bus = 20.0 + 3.0 * (std::f32::consts::PI * seconds).sin();
            (v_shunt, v_bus)
        }

        // These values are in the same ballpark as those used on Zedmon 2.1. The test shouldn't be
        // sensitive to them.
        let shunt_resistance = 0.01;
        let v_shunt_scale = 1e-5;
        let v_bus_scale = 0.025;

        let runner = ZedmonRecordRunner {
            voltage_function: Box::new(get_voltages),
            shunt_resistance,
            v_shunt_scale,
            v_bus_scale,
            test_duration: Duration::from_secs(10),
            reporting_interval: Duration::from_millis(1),
        };
        let output = runner.run()?;

        let mut num_lines = 0;
        for line in output.lines() {
            num_lines = num_lines + 1;

            let parts: Vec<&str> = line.split(",").collect();
            assert_eq!(4, parts.len());
            let timestamp: u64 = parts[0].parse()?;
            let v_shunt_out: f32 = parts[1].parse()?;
            let v_bus_out: f32 = parts[2].parse()?;

            let (v_shunt_expected, v_bus_expected) = get_voltages(timestamp);
            assert_near!(v_shunt_out, v_shunt_expected, v_shunt_scale);
            assert_near!(v_bus_out, v_bus_expected, v_bus_scale);

            let power_out: f32 = parts[3].parse()?;
            assert_near!(power_out, v_shunt_out * v_bus_out / shunt_resistance, 1e-6);
        }

        assert_eq!(num_lines, 10000);

        Ok(())
    }
}
