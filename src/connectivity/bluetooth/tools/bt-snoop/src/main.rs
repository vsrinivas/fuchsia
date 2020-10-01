// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    fidl::endpoints::Proxy,
    fidl::Error as FidlError,
    fidl_fuchsia_bluetooth_snoop::{SnoopPacket, SnoopRequest, SnoopRequestStream},
    fidl_fuchsia_io, fuchsia_async as fasync,
    fuchsia_bluetooth::bt_fidl_status,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect, fuchsia_trace as trace,
    fuchsia_vfs_watcher::{WatchEvent, WatchMessage, Watcher},
    futures::{
        future::{join, ready, Join, Ready},
        select,
        stream::{FusedStream, FuturesUnordered, Stream, StreamExt, StreamFuture},
    },
    log::{debug, error, info, trace, warn},
    std::{
        fmt,
        fs::File,
        path::{Path, PathBuf},
        time::Duration,
    },
};

use crate::{packet_logs::PacketLogs, snooper::Snooper, subscription_manager::SubscriptionManager};

mod bounded_queue;
mod packet_logs;
mod snooper;
mod subscription_manager;
#[cfg(test)]
mod tests;

/// Root directory of all HCI devices
const HCI_DEVICE_CLASS_PATH: &str = "/dev/class/bt-hci";

/// A `DeviceId` represents the name of a host device within the HCI_DEVICE_CLASS_PATH.
pub(crate) type DeviceId = String;

/// A request is a tuple of the client id, the optional next request, and the rest of the stream.
type ClientRequest = (ClientId, (Option<Result<SnoopRequest, FidlError>>, SnoopRequestStream));

/// A `Stream` that holds a collection of client request streams and will return the item from the
/// next ready stream.
type ConcurrentClientRequestFutures =
    FuturesUnordered<Join<Ready<ClientId>, StreamFuture<SnoopRequestStream>>>;

/// A `Stream` that holds a collection of snooper streams and will return the item from the
/// next ready stream.
type ConcurrentSnooperPacketFutures = FuturesUnordered<StreamFuture<Snooper>>;

/// A `ClientId` represents the unique identifier for a client that has connected to the bt-snoop
/// service.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub(crate) struct ClientId(u64);

impl fmt::Display for ClientId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// Generates 64-bit ids in increasing order with wrap around behavior at `u64::MAX`
/// Ids will be unique, as long as there is not a client that lives longer than the
/// next 2^63-1 clients.
struct IdGenerator(ClientId);

impl IdGenerator {
    fn new() -> IdGenerator {
        IdGenerator(ClientId(0))
    }
    fn next(&mut self) -> ClientId {
        let id = self.0;
        (self.0).0 = (self.0).0.wrapping_add(1);
        id
    }
}

/// Create a lossy `String` clone of a `Path`
fn path_to_string(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

fn absolute_device_path<P: AsRef<Path>>(dev_name: &P) -> PathBuf {
    let mut path = PathBuf::from(HCI_DEVICE_CLASS_PATH);
    path.push(dev_name);
    path
}

/// Handle an event on the virtual filesystem in the HCI device directory. This should log or
/// internally handle most errors that come from the stream of filesystem watch events. Only errors
/// in the `Watcher` itself result in returning an Error to the caller.
fn handle_hci_device_event(
    message: WatchMessage,
    snoopers: &mut ConcurrentSnooperPacketFutures,
    subscribers: &mut SubscriptionManager,
    packet_logs: &mut PacketLogs,
) {
    let path = absolute_device_path(&message.filename);

    match message.event {
        WatchEvent::ADD_FILE | WatchEvent::EXISTING => {
            info!("Opening snoop channel for hci device \"{}\"", path.display());
            println!("Getting vfs event");
            match Snooper::new(path.clone()) {
                Ok(snooper) => {
                    snoopers.push(snooper.into_future());
                    let removed_device = packet_logs.add_device(path_to_string(&message.filename));
                    if let Some(device) = removed_device {
                        subscribers.remove_device(&device);
                    }
                }
                Err(e) => {
                    println!("failed");
                    warn!("Failed to open snoop channel for \"{}\": {}", path.display(), e);
                }
            }
        }
        WatchEvent::REMOVE_FILE => {
            info!("Removing snoop channel for hci device: \"{}\"", path.display());
            // TODO(belgum): What should be done with the logged packets in this case?
            //               Find out how to remove snooper from ConcurrentTask (perhaps cancel
            //               and wake)
            //               Can possibly reopen device logs for devices that are on disk that
            //               were evicted from the packet logs collection in the past.
        }
        _ => (),
    }
}

/// Register a new client.
fn register_new_client(
    stream: SnoopRequestStream,
    client_stream: &mut ConcurrentClientRequestFutures,
    client_id: ClientId,
) {
    client_stream.push(join(ready(client_id), stream.into_future()));
    info!("New client connection: {}", client_id);
}

/// Handle a client request to dump the packet log, subscribe to future events or do both.
/// Returns an error if the client channel does not accept a response that it requested.
fn handle_client_request(
    request: ClientRequest,
    client_requests: &mut ConcurrentClientRequestFutures,
    subscribers: &mut SubscriptionManager,
    packet_logs: &mut PacketLogs,
) -> Result<(), Error> {
    let (id, (request, client_stream)) = request;
    match request {
        Some(Ok(SnoopRequest::Start { follow, host_device, responder })) => {
            // Return early if the client has already issued a `Start` request.
            if subscribers.is_registered(&id) {
                responder.send(&mut bt_fidl_status!(
                    Already,
                    "Cannot issue `Start` request more than once."
                ))?;
                return Ok(());
            }

            debug!("Request received from client {}.", id);

            let control_handle = responder.control_handle().clone();

            if let Some(ref device) = host_device {
                if let Some(log) = packet_logs.get_log_mut(device) {
                    responder.send(&mut bt_fidl_status!())?;
                    for packet in log.iter_mut() {
                        control_handle.send_on_packet(device, packet)?;
                    }
                } else {
                    responder.send(&mut bt_fidl_status!(NotFound, "Unrecognized device name."))?;
                    return Ok(());
                }
            } else {
                responder.send(&mut bt_fidl_status!())?;
                let device_ids: Vec<_> = packet_logs.device_ids().cloned().collect();
                for device in &device_ids {
                    if let Some(log) = packet_logs.get_log_mut(device) {
                        for packet in log.iter_mut() {
                            control_handle.send_on_packet(device, packet)?;
                        }
                    }
                }
            }

            if follow {
                subscribers
                    .register(id, control_handle, host_device)
                    .expect("A client `Start` request should never be processed more than once");
                client_requests.push(join(ready(id), client_stream.into_future()));
                trace!("Client {} subscribed and waiting", id);
            } else {
                trace!("Client {} shutting down", id);
                control_handle.shutdown();
            }
        }
        Some(Err(e)) => {
            warn!("Client returned error: {:?}", e);
            subscribers.deregister(&id);
        }
        None => {
            debug!("Client disconnected");
            subscribers.deregister(&id);
        }
    }
    Ok(())
}

/// Handle a possible incoming packet. Returns an error if the snoop channel is closed and cannot
/// be reopened.
fn handle_packet(
    packet: Option<(DeviceId, SnoopPacket)>,
    snooper: Snooper,
    snoopers: &mut ConcurrentSnooperPacketFutures,
    subscribers: &mut SubscriptionManager,
    packet_logs: &mut PacketLogs,
    truncate_payload: Option<usize>,
) {
    if let Some((device, mut packet)) = packet {
        trace!("Received packet from {:?}.", snooper.device_path);
        if let Some(len) = truncate_payload {
            packet.payload.truncate(len);
        }
        subscribers.notify(&device, &mut packet);
        packet_logs.log_packet(&device, packet);
        snoopers.push(snooper.into_future());
    } else {
        info!("Snoop channel closed for device: {}", snooper.device_name);
    }
}

struct SnoopConfig {
    log_size_soft_max_bytes: usize,
    log_size_hard_max_bytes: usize,
    log_time: Duration,
    max_device_count: usize,
    truncate_payload: Option<usize>,

    // Inspect tree
    _config_inspect: inspect::Node,
    _log_size_soft_max_bytes_property: inspect::UintProperty,
    _log_size_hard_max_bytes_property: inspect::StringProperty,
    _log_time_property: inspect::UintProperty,
    _max_device_count_property: inspect::UintProperty,
    _truncate_payload_property: inspect::StringProperty,
    _hci_dir_property: inspect::StringProperty,
}

impl SnoopConfig {
    /// Creates a strongly typed `SnoopConfig` out of primitives parsed from the command line
    fn from_args(args: Args, config_inspect: inspect::Node) -> SnoopConfig {
        let log_size_soft_max_bytes = args.log_size_soft_kib * 1024;
        let log_size_hard_max_bytes = args.log_size_hard_kib * 1024;
        let log_time = Duration::from_secs(args.log_time_seconds);
        let _log_size_soft_max_bytes_property =
            config_inspect.create_uint("log_size_soft_max_bytes", log_size_soft_max_bytes as u64);
        let hard_max = if log_size_hard_max_bytes == 0 {
            "No Hard Max".to_string()
        } else {
            log_size_hard_max_bytes.to_string()
        };
        let _log_size_hard_max_bytes_property =
            config_inspect.create_string("log_size_hard_max_bytes", &hard_max);
        let _log_time_property = config_inspect.create_uint("log_time", log_time.as_secs());
        let _max_device_count_property =
            config_inspect.create_uint("max_device_count", args.max_device_count as u64);
        let truncate = args
            .truncate_payload
            .as_ref()
            .map(|n| format!("{} bytes", n))
            .unwrap_or("No Truncation".to_string());
        let _truncate_payload_property =
            config_inspect.create_string("truncate_payload", &truncate);
        let _hci_dir_property = config_inspect.create_string("hci_dir", HCI_DEVICE_CLASS_PATH);

        SnoopConfig {
            log_size_soft_max_bytes,
            log_size_hard_max_bytes,
            log_time,
            max_device_count: args.max_device_count,
            truncate_payload: args.truncate_payload,
            _config_inspect: config_inspect,
            _log_size_soft_max_bytes_property,
            _log_size_hard_max_bytes_property,
            _log_time_property,
            _max_device_count_property,
            _truncate_payload_property,
            _hci_dir_property,
        }
    }
}

#[derive(FromArgs)]
/// Log bluetooth snoop packets and provide them to clients.
struct Args {
    #[argh(option, default = "32")]
    /// packet storage buffer size after which packets will start aging off.
    log_size_soft_kib: usize,
    #[argh(option, default = "256")]
    /// hard maximum size in KiB of the buffer to store packets in.
    /// a value of "0" indicates no limit. Defaults to 0.
    log_size_hard_kib: usize,
    #[argh(option, default = "60")]
    /// minimum time to store packets in a snoop log in seconds.
    log_time_seconds: u64,
    #[argh(option, default = "8")]
    /// maximum number of devices for which to store logs.
    max_device_count: usize,
    #[argh(option)]
    /// maximum number of bytes to keep in the payload of incoming packets. Defaults to no limit.
    truncate_payload: Option<usize>,
    #[argh(switch, short = 'v')]
    /// enable verbose log output. Using twice will increase verbosity.
    verbosity: u16,
}

/// Setup the main loop of execution in a Task and run it.
async fn run(
    config: SnoopConfig,
    mut service_handler: impl Unpin + FusedStream + Stream<Item = SnoopRequestStream>,
    inspect: inspect::Node,
) -> Result<(), Error> {
    let mut id_gen = IdGenerator::new();
    let hci_dir = File::open(HCI_DEVICE_CLASS_PATH).expect("Failed to open hci dev directory");
    let channel = fdio::clone_channel(&hci_dir)?;
    let async_channel = fasync::Channel::from_channel(channel)?;
    let directory = fidl_fuchsia_io::DirectoryProxy::from_channel(async_channel);
    let mut hci_device_events = Watcher::new(io_util::clone_directory(
        &directory,
        fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS,
    )?)
    .await
    .context("Cannot create device watcher")?;
    let mut client_requests = ConcurrentClientRequestFutures::new();
    let mut subscribers = SubscriptionManager::new();
    let mut snoopers = ConcurrentSnooperPacketFutures::new();
    let mut packet_logs = PacketLogs::new(
        config.max_device_count,
        config.log_size_soft_max_bytes,
        config.log_size_hard_max_bytes,
        config.log_time,
        inspect,
    );

    debug!("Capturing snoop packets...");

    loop {
        select! {
            // A new client has connected to one of the exposed services.
            request_stream = service_handler.select_next_some() => {
                register_new_client(request_stream, &mut client_requests, id_gen.next());
            },

            // A new filesystem event in the hci device watch directory has been received.
            event = hci_device_events.next() => {
                let message = event
                    .ok_or(format_err!("Cannot reach watch server"))
                    .and_then(|r| Ok(r?));
                match message {
                    Ok(message) => {
                        handle_hci_device_event(message, &mut snoopers, &mut subscribers,
                            &mut packet_logs);
                    }
                    Err(e) => {
                        // Attempt to recreate watcher in the event of an error.
                        warn!("VFS Watcher has died with error: {:?}", e);
                        hci_device_events = Watcher::new(io_util::clone_directory(
                            &directory,
                            fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS,
                        )?)
                            .await
                            .context("Cannot create device watcher")?;
                    }
                }
            },

            // A client has made a request to the server.
            request = client_requests.select_next_some() => {
                if let Err(e) = handle_client_request(request, &mut client_requests,
                    &mut subscribers, &mut packet_logs)
                {
                    debug!("Unable to handle client request: {:?}", e);
                }
            },

            // A new snoop packet has been received from an hci device.
            (packet, snooper) = snoopers.select_next_some() => {
                trace::duration!("bluetooth", "Snoop::ProcessPacket");
                handle_packet(packet, snooper, &mut snoopers, &mut subscribers,
                    &mut packet_logs, config.truncate_payload);
            },
        }
    }
}

/// Initializes syslog with tags and verbosity
///
/// Panics if syslog logger cannot be initialized
fn init_logging(verbosity: u16) {
    fuchsia_syslog::init_with_tags(&["bt-snoop"]).expect("Can't init logger");
    if verbosity > 1 {
        fuchsia_syslog::set_severity(fuchsia_syslog::levels::TRACE);
    } else if verbosity > 0 {
        fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);
    }
    info!("Starting bt-snoop.");
}

/// Parse program arguments, call the main loop, and log any unrecoverable errors.
#[fasync::run_singlethreaded]
async fn main() {
    let args: Args = argh::from_env();

    fuchsia_trace_provider::trace_provider_create_with_fdio();
    init_logging(args.verbosity);

    let mut fs = ServiceFs::new();

    let inspector = inspect::Inspector::new();
    inspector.serve(&mut fs).unwrap_or_else(|e| {
        error!("Failed to serve the inspect tree: {:?}", e);
    });

    let config_inspect = inspector.root().create_child("configuration");
    let runtime_inspect = inspector.root().create_child("runtime_metrics");

    let config = SnoopConfig::from_args(args, config_inspect);

    fs.dir("svc").add_fidl_service(|stream: SnoopRequestStream| stream);

    fs.take_and_serve_directory_handle().expect("serve ServiceFS directory");

    match run(config, fs.fuse(), runtime_inspect).await {
        Err(err) => error!("Failed with critical error: {:?}", err),
        _ => {}
    };
}
