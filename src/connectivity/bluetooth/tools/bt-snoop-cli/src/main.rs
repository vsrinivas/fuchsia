// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    byteorder::{BigEndian, WriteBytesExt},
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_snoop::{PacketType, SnoopEvent, SnoopMarker, SnoopPacket},
    fuchsia_async as fasync,
    fuchsia_bluetooth::error::Error as BTError,
    fuchsia_component::client::connect_to_service,
    futures::TryStreamExt,
    std::{fmt, fs::File, io, path::Path},
};

const PCAP_CMD: u8 = 0x01;
const PCAP_DATA: u8 = 0x02;
const PCAP_EVENT: u8 = 0x04;

enum Format {
    Pcap,
    Pretty,
}

impl fmt::Display for Format {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let format = match self {
            Format::Pcap => "pcap",
            Format::Pretty => "pretty",
        };
        write!(f, "{}", format)
    }
}

impl std::str::FromStr for Format {
    type Err = std::convert::Infallible;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(match s {
            "pcap" => Format::Pcap,
            "pretty" => Format::Pretty,
            _ => {
                eprintln!("Unrecognized format. Using pcap");
                Format::Pcap
            }
        })
    }
}

// Format described in https://wiki.wireshark.org/Development/LibpcapFileFormat#Global_Header
pub fn pcap_header() -> Vec<u8> {
    let mut wtr = vec![];
    wtr.write_u32::<BigEndian>(0xa1b2c3d4).unwrap(); // Magic number
    wtr.write_u16::<BigEndian>(2).unwrap(); // Major Version
    wtr.write_u16::<BigEndian>(4).unwrap(); // Minor Version
    wtr.write_i32::<BigEndian>(0).unwrap(); // Timezone: GMT
    wtr.write_u32::<BigEndian>(0).unwrap(); // Sigfigs
    wtr.write_u32::<BigEndian>(65535).unwrap(); // Max packet length
    wtr.write_u32::<BigEndian>(201).unwrap(); // Protocol: BLUETOOTH_HCI_H4_WITH_PHDR
    wtr
}

// Format described in
// https://wiki.wireshark.org/Development/LibpcapFileFormat#Record_.28Packet.29_Header
pub fn to_pcap_fmt(pkt: SnoopPacket) -> Vec<u8> {
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

// Pretty print packet metadata with hex representation of payload
fn to_pretty_fmt(pkt: SnoopPacket) -> String {
    let payload =
        pkt.payload.iter().map(|byte| format!("{:x}", byte)).collect::<Vec<String>>().join("");
    format!(
        "{}.{:09}: {:5} {} {}\n",
        pkt.timestamp.seconds,
        pkt.timestamp.subsec_nanos,
        format!("{:?}", pkt.type_),
        if pkt.is_received { "RX" } else { "TX" },
        payload
    )
}

/// Define the command line arguments that the tool accepts.
#[derive(FromArgs)]
#[argh(description = "Snoop Bluetooth controller packets")]
struct Opt {
    #[argh(switch, short = 'd')]
    /// dump the available history of snoop packets
    dump: bool,

    #[argh(option, short = 'f', default = "Format::Pcap")]
    /// file format. options: [pcap, pretty]. Defaults to `pcap`.
    format: Format,

    #[argh(option, short = 'c')]
    /// exit after N packets have been recorded
    count: Option<u64>,

    #[argh(option)]
    /// request snoop log for a single device by name.
    device: Option<String>,

    #[argh(option, short = 'o')]
    /// output location. Default: stdout
    output: Option<String>,

    #[argh(option, short = 't')]
    /// truncate packets to N bytes before outputting them
    truncate: Option<usize>,
}

/// Construct and print a human friendly message relaying the behavior the tool has been invoked
/// to use.
fn print_opts(opts: &Opt) {
    let action = if opts.dump { "Dumping" } else { "Following" };
    let device = if let Some(ref device) = opts.device { device } else { "all devices" };
    let truncate = if let Some(size) = opts.truncate {
        format!("Truncating packets to {} bytes. ", size)
    } else {
        String::new()
    };
    let count =
        if opts.count.is_some() { format!("up to {} ", opts.count.unwrap()) } else { "".into() };
    eprintln!(
        "{action} snoop log for \"{device}\". {truncation}Outputting {count}packets to {output}.",
        action = action,
        device = device,
        truncation = truncate,
        count = count,
        output = opts.output.as_ref().unwrap_or(&"stdout".to_string()),
    );
}

fn main_res() -> Result<(), Error> {
    // Parse and transform command line arguments.
    let opts: Opt = argh::from_env();

    print_opts(&opts);

    let Opt { dump, format, truncate, count, device, output } = opts;

    let follow = !dump;

    let mut out = match output {
        Some(ref s) => {
            let path = Path::new(s);
            Box::new(File::create(&path).unwrap()) as Box<dyn io::Write>
        }
        None => Box::new(io::stdout()) as Box<dyn io::Write>,
    };

    // create and run the main future
    let main_future = async {
        let snoop_svc = connect_to_service::<SnoopMarker>()
            .context("failed to connect to bluetooth snoop interface")?;
        let mut evt_stream = snoop_svc.take_event_stream();
        match format {
            Format::Pcap => out.write(pcap_header().as_slice())?,
            Format::Pretty => 0,
        };
        out.flush()?;

        // transform device from Option<String> to Option<&str>
        let dev_str = device.as_ref().map(|d| d.as_str());

        // Send request to start receiving snoop packets
        let status = snoop_svc.start(follow, dev_str).await?;
        if let Some(e) = status.error {
            return Err(BTError::from(*e).into());
        }

        // Receive snoop packet events and output them in the requested format.
        let mut pkt_count = 0;
        while let Some(evt) = evt_stream.try_next().await.expect("failed to fetch event") {
            let SnoopEvent::OnPacket { host_device: _, mut packet } = evt;
            if let Some(size) = truncate {
                packet.payload.truncate(size);
            }
            match format {
                Format::Pcap => out.write(to_pcap_fmt(packet).as_slice())?,
                Format::Pretty => out.write(to_pretty_fmt(packet).as_bytes())?,
            };
            out.flush()?;
            if let Some(count) = count {
                pkt_count += 1;
                if pkt_count == count {
                    break;
                }
            }
        }
        Ok(())
    };

    fasync::Executor::new().context("error creating event loop")?.run_singlethreaded(main_future)
}

fn main() {
    if let Err(e) = main_res() {
        eprintln!("Error: {}", e);
    }
}
