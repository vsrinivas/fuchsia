// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

mod pcapng;

use anyhow::Context as _;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, HandleBased as _};
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use packet::{NestedPacketBuilder as _, ParsablePacket as _, Serializer as _};
use packet_formats::{ip::IpPacketBuilder as _, ipv4::Ipv4Header as _};
use std::convert::TryInto as _;
use vfs::directory::entry::DirectoryEntry as _;

type Result<T = ()> = std::result::Result<T, anyhow::Error>;

/// Endpoint name given to netemul.
const EP_NAME: &'static str = "test-ep";
/// Network name given to netemul.
const NET_NAME: &'static str = "test-net";
/// Mount path for endpoint under `/vdev` in the isolated environment.
const EP_MOUNT_PATH: &'static str = "class/ethernet/test";

/// Common number of packets observed during tests.
const PACKET_COUNT: usize = 10;

/// The name that `DUMPFILE_DIR` takes when forwarded to netdump's namespace.
const NETDUMP_DUMPFILE_DIR: &str = "/out";
const NETDUMP_DUMPFILE_NAME: &str = "dump";

const NETDUMP_URL: &str = fuchsia_component::fuchsia_single_component_package_url!("netdump");

/// Default source IPv4 address used in tests.
const SRC_IP_V4: std::net::Ipv4Addr = net_declare::std_ip_v4!(192.168.0.1);
/// Default destination IPv4 address used in tests.
const DST_IP_V4: std::net::Ipv4Addr = net_declare::std_ip_v4!(192.168.0.2);
/// Default UDP source port used in tests.
const SRC_PORT: std::num::NonZeroU16 = unsafe { std::num::NonZeroU16::new_unchecked(1010) };
/// Default UDP destination port used in tests.
const DST_PORT: std::num::NonZeroU16 = unsafe { std::num::NonZeroU16::new_unchecked(8080) };

/// The capture line produced by netdump when using the default IPv4 packet
/// parameters above and a 100 byte UDP payload.
const DEFAULT_UDP_IPV4_OUTPUT: &'static str =
    "IP4 192.168.0.1 > 192.168.0.2: UDP, length 128, Ports: 1010 > 8080";

/// Source MAC address set by [`build_udp_packet`].
const SRC_MAC_BYTES: [u8; 6] = [0x02, 0x0AA, 0xBB, 0xCC, 0xEE, 0xFF];
/// Destination MAC address set by [`build_udp_packet`].
const DST_MAC_BYTES: [u8; 6] = [0x02, 0x00A, 0x0B, 0x0C, 0x0E, 0x0F];
/// Buffer allocated for in memory vfs.
const DUMPFILE_BUFFER_LEN: u64 = 64 << 10;

/// Helper for constructing valid command-line arguments to netdump.
#[derive(Default)]
pub struct Args {
    device: Option<String>,
    packet_count: Option<usize>,
    timeout_secs: Option<u64>,
    filter: Option<String>,
    write_to_dumpfile: bool,
    pcapng_to_stdout: bool,
}

impl From<Args> for Vec<String> {
    fn from(input: Args) -> Vec<String> {
        let Args {
            device,
            packet_count,
            timeout_secs,
            filter,
            write_to_dumpfile,
            pcapng_to_stdout,
        } = input;
        let mut options = Vec::new();
        if pcapng_to_stdout {
            options.push("--pcapdump".into());
        }
        if write_to_dumpfile {
            options.push("-w".into());
            options.push(format!("{}/{}", NETDUMP_DUMPFILE_DIR, NETDUMP_DUMPFILE_NAME));
        }
        if let Some(filter) = filter {
            options.push("-f".into());
            options.push(filter);
        }
        if let Some(timeout) = timeout_secs {
            options.push("-t".into());
            options.push(timeout.to_string());
        }
        if let Some(packet_count) = packet_count {
            options.push("-c".into());
            options.push(packet_count.to_string());
        }
        if let Some(device) = device {
            options.push(device);
        }
        options
    }
}

impl Args {
    /// New set of arguments with the default device path.
    fn new() -> Self {
        Self::new_with_device(
            std::path::Path::new("")
                .join("vdev")
                .join(EP_MOUNT_PATH)
                .into_os_string()
                .into_string()
                .expect("failed to create device path"),
        )
    }

    /// Returns an empty set of arguments.
    fn empty() -> Self {
        Self::default()
    }

    /// New set of arguments given an endpoint path of the device to sniff.
    fn new_with_device(path: impl Into<String>) -> Self {
        Args {
            device: Some(path.into()),
            packet_count: None,
            timeout_secs: None,
            filter: None,
            write_to_dumpfile: false,
            pcapng_to_stdout: false,
        }
    }
    /// Helper method for inserting a packet count argument.
    fn insert_packet_count(mut self, count: usize) -> Self {
        self.packet_count = Some(count);
        self
    }

    /// Helper method for inserting a filter argument.
    fn insert_filter(mut self, filter: &str) -> Self {
        self.filter = Some(filter.into());
        self
    }

    /// Helper method for inserting a timeout argument.
    fn insert_timeout(mut self, timeout_secs: u64) -> Self {
        self.timeout_secs = Some(timeout_secs);
        self
    }

    /// Helper method for inserting a write to pcapng dumpfile argument.
    fn insert_write_to_dumpfile(mut self) -> Self {
        self.write_to_dumpfile = true;
        self
    }

    /// Helper method for inserting a pcapng dump to stdout argument.
    fn insert_pcapng_dump_to_stdout(mut self) -> Self {
        self.pcapng_to_stdout = true;
        self
    }
}

/// A test environment for netdump backed by Netemul.
#[must_use]
struct TestEnvironment {
    sandbox: fidl_fuchsia_netemul_sandbox::SandboxProxy,
    environment: fidl_fuchsia_netemul_environment::ManagedEnvironmentProxy,
}

impl TestEnvironment {
    /// Creates a given environment with `name`.
    fn new(name: impl Into<String>) -> Result<Self> {
        let sandbox = fuchsia_component::client::connect_to_service::<
            fidl_fuchsia_netemul_sandbox::SandboxMarker,
        >()
        .context("failed to connect to sandbox")?;
        let (environment, server) = fidl::endpoints::create_proxy::<
            fidl_fuchsia_netemul_environment::ManagedEnvironmentMarker,
        >()?;
        let () = sandbox
            .create_environment(
                server,
                fidl_fuchsia_netemul_environment::EnvironmentOptions {
                    name: Some(name.into()),
                    services: None,
                    devices: None,
                    inherit_parent_launch_services: None,
                    logger_options: None,
                },
            )
            .context("failed to create environment")?;

        Ok(TestEnvironment { sandbox, environment })
    }

    /// Creates a networked endpoint in this environment.
    ///
    /// Automatically attaches the endpoint to the environments `/vdev`
    /// directory under [`EP_MOUNT_PATH`].
    async fn create_endpoint(&self) -> Result<TestNetwork> {
        let (netctx, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_network::NetworkContextMarker>()?;
        let () = self.sandbox.get_network_context(server)?;

        let (network_manager, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_network::NetworkManagerMarker>()?;
        let () = netctx.get_network_manager(server)?;

        let (ep_manager, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_network::EndpointManagerMarker>()?;
        let () = netctx.get_endpoint_manager(server)?;

        let (status, network) = network_manager
            .create_network(
                NET_NAME,
                fidl_fuchsia_netemul_network::NetworkConfig {
                    latency: None,
                    packet_loss: None,
                    reorder: None,
                },
            )
            .await
            .context("create network FIDL error")?;
        let () = zx::Status::ok(status).context("create network failed")?;
        let network = network.ok_or_else(|| anyhow::anyhow!("missing network in response"))?;
        let network = network.into_proxy()?;

        let (status, endpoint) = ep_manager
            .create_endpoint(
                &EP_NAME,
                &mut fidl_fuchsia_netemul_network::EndpointConfig {
                    mtu: 1500,
                    mac: None,
                    backing: fidl_fuchsia_netemul_network::EndpointBacking::Ethertap,
                },
            )
            .await
            .context("create endpoint FIDL error")?;
        let () = zx::Status::ok(status).context("create endpoint failed")?;
        let endpoint = endpoint.ok_or_else(|| anyhow::anyhow!("missing network in response"))?;
        let endpoint = endpoint.into_proxy()?;
        let () = endpoint.set_link_up(true).await.context("failed to set link up")?;
        let (ep_proxy, server) = fidl::endpoints::create_endpoints::<
            fidl_fuchsia_netemul_network::DeviceProxy_Marker,
        >()?;
        let () = endpoint.get_proxy_(server)?;
        let () = self
            .environment
            .add_device(&mut fidl_fuchsia_netemul_environment::VirtualDevice {
                path: EP_MOUNT_PATH.to_string(),
                device: ep_proxy,
            })
            .context("failed to add device to environment")?;

        let () = zx::Status::ok(
            network.attach_endpoint(EP_NAME).await.context("attach_endpoint FIDL error")?,
        )
        .context("attach_endpoint failed")?;

        Ok(TestNetwork { network, endpoint: endpoint })
    }

    /// Launches netdump with provided `args` and optional `dump_contents`
    /// vector that will contain the dumpfile contents.
    ///
    /// On success, returns a future that resolves with netdump output and
    /// return codes and a handle to the directory request channel given to
    /// netdump.
    fn launch_netdump(
        &self,
        args: Vec<String>,
        dump_contents: Option<std::sync::Arc<std::sync::Mutex<Vec<u8>>>>,
    ) -> Result<(
        impl futures::Future<Output = Result<fuchsia_component::client::Output>> + std::marker::Unpin,
        std::sync::Arc<zx::Channel>,
    )> {
        let (launcher, launcher_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_sys::LauncherMarker>()?;
        let () =
            self.environment.get_launcher(launcher_server_end).context("failed to get launcher")?;

        let mut app_builder =
            fuchsia_component::client::AppBuilder::new(NETDUMP_URL.to_string()).args(args);
        if let Some(dump_contents) = dump_contents {
            let dir = vfs::pseudo_directory! {
               NETDUMP_DUMPFILE_NAME => vfs::file::pcb::write_only(
                  DUMPFILE_BUFFER_LEN,
                  move |content| {
                      let content = content.to_owned();
                      let mut outer = dump_contents.lock().unwrap();
                      let _old_content = std::mem::replace(outer.as_mut(), content.to_owned());
                      futures::future::ok(())
                  })
            };

            let (client_dir, server_dir) =
                fidl::endpoints::create_endpoints::<fidl_fuchsia_io::NodeMarker>()?;
            let scope = vfs::execution_scope::ExecutionScope::new();
            dir.open(
                scope,
                fidl_fuchsia_io::OPEN_FLAG_DIRECTORY
                    | fidl_fuchsia_io::OPEN_FLAG_CREATE
                    | fidl_fuchsia_io::OPEN_RIGHT_READABLE
                    | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
                0,
                vfs::path::Path::empty(),
                server_dir,
            );
            app_builder = app_builder.add_handle_to_namespace(
                NETDUMP_DUMPFILE_DIR.into(),
                client_dir.into_channel().into_handle(),
            );
        }
        let dir_request = std::sync::Arc::clone(
            app_builder.directory_request().context("failed to create directory request")?,
        );
        let app_fut = app_builder.output(&launcher).context("failed to launch")?;
        Ok((app_fut, dir_request))
    }

    /// Runs netdump with `args` and calls `f` when it's ready to listen to
    /// packets.
    ///
    /// On success, returns netdump's output and the dumpfile's contents. Note
    /// that the dumpfile will be an empty vector if `args` was not set for
    /// dumpfile output.
    async fn run_netdump<F, Fut>(
        &self,
        args: Vec<String>,
        f: F,
    ) -> Result<(fuchsia_component::client::Output, Vec<u8>)>
    where
        F: FnOnce() -> Fut,
        Fut: futures::Future<Output = Result>,
    {
        let dump_result = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
        let (app, dir_request) = self
            .launch_netdump(args, Some(dump_result.clone()))
            .context("failed to launch netdump")?;
        let mut app = app.fuse();
        // Netdump will close the directory request when it's ready to start
        // operating packets.
        let mut ready =
            fuchsia_async::OnSignals::new(dir_request.as_ref(), zx::Signals::CHANNEL_PEER_CLOSED)
                .fuse();
        futures::select! {
            output = app => {
                let output = output.context("app future terminated with error")?;
                return Err(anyhow::anyhow!("netdump exited before we saw an directory request close: {:?}", output.exit_status))
            },
            r = ready => { let _: zx::Signals = r.context("failed to wait on directory request close")?; }
        }
        let ((), output) = futures::future::try_join(f(), app).await?;

        let dump_result = std::mem::replace(dump_result.lock().unwrap().as_mut(), Vec::new());
        Ok((output, dump_result))
    }
}

/// Helper trait to define common test values over IP versions.
trait IpExt: packet_formats::ip::IpExt {
    /// The source IP address used in tests.
    fn src_ip() -> Self::Addr;
    /// The destination IP address used in tests.
    fn dst_ip() -> Self::Addr;
}

impl IpExt for net_types::ip::Ipv4 {
    fn src_ip() -> Self::Addr {
        net_types::ip::Ipv4Addr::new(SRC_IP_V4.octets())
    }

    fn dst_ip() -> Self::Addr {
        net_types::ip::Ipv4Addr::new(DST_IP_V4.octets())
    }
}

/// Builds a UDP packet of the form:
///
/// source tuple = `I::src_ip()`:`src_port`.
/// destination tuple = `I::dst_ip()`:`dst_port`.
///
/// The body of the UDP packet is filled with `payload_length` zero bytes.
fn build_udp_packet<I: IpExt>(
    payload_length: usize,
    src_port: Option<std::num::NonZeroU16>,
    dst_port: std::num::NonZeroU16,
) -> Vec<u8> {
    let mut buf: Vec<u8> = std::iter::repeat(0).take(payload_length).collect();
    let src_mac = net_types::ethernet::Mac::new(SRC_MAC_BYTES);
    let dst_mac = net_types::ethernet::Mac::new(DST_MAC_BYTES);
    packet::Buf::new(&mut buf[..], ..)
        .encapsulate(packet_formats::udp::UdpPacketBuilder::new(
            I::src_ip(),
            I::dst_ip(),
            src_port,
            dst_port,
        ))
        .encapsulate(
            I::PacketBuilder::new(I::src_ip(), I::dst_ip(), 1, packet_formats::ip::IpProto::Udp)
                .encapsulate(packet_formats::ethernet::EthernetFrameBuilder::new(
                    src_mac,
                    dst_mac,
                    packet_formats::ethernet::EtherType::Ipv4,
                )),
        )
        .serialize_vec_outer()
        .expect("serialization into vec can't fail")
        .as_ref()
        .to_vec()
}

/// Helper function to excite netdump's Rx path with UDP packets.
///
/// `fake_ep` is the endpoint used to send frames through to the Rx Path.
/// `count` is how many of the same packets to send.
/// `payload_length` is the length of the UDP packet's body.
///
/// The packets follow the description in [`build_udp_packet`] and always use
/// [`SRC_PORT`] and [`DST_PORT`].
async fn receive_udp_packets<I: IpExt>(
    fake_ep: &fidl_fuchsia_netemul_network::FakeEndpointProxy,
    count: usize,
    payload_length: usize,
) -> Result {
    let packet = build_udp_packet::<I>(payload_length, Some(SRC_PORT), DST_PORT);
    futures::stream::repeat(())
        .map(Ok)
        .take(count)
        .try_for_each_concurrent(None, move |()| {
            fake_ep.write(&packet[..]).map(|result| result.context("fake ep write failed"))
        })
        .await
}

/// A single-endpoint network backed by netemul.
#[must_use]
struct TestNetwork {
    network: fidl_fuchsia_netemul_network::NetworkProxy,
    endpoint: fidl_fuchsia_netemul_network::EndpointProxy,
}

impl TestNetwork {
    /// Creates a fake endpoint attached to the network.
    ///
    /// Writing to the fake endpoint can is used to excite netdump's Rx
    /// capturing path.
    fn create_fake_endpoint(&self) -> Result<fidl_fuchsia_netemul_network::FakeEndpointProxy> {
        let (endpoint, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_netemul_network::FakeEndpointMarker>()
                .context("failed to create launcher proxy")?;
        let () = self.network.create_fake_endpoint(server)?;
        Ok(endpoint)
    }

    /// Gets an ethernet client for the Network's endpoint.
    ///
    /// Sending frames over [`ethernet::Client`] can be used to excite netdump's
    /// Tx capturing path.
    async fn get_ethernet_client(&self) -> Result<ethernet::Client> {
        let eth = match self.endpoint.get_device().await.context("failed to get endpoint device")? {
            fidl_fuchsia_netemul_network::DeviceConnection::Ethernet(eth_device) => eth_device,
            fidl_fuchsia_netemul_network::DeviceConnection::NetworkDevice(_netdevice) => {
                return Err(anyhow::anyhow!("Network device not supported"))
            }
        };
        const BUF_SIZE: usize = 1500;
        let vmo = zx::Vmo::create((BUF_SIZE * 2 * PACKET_COUNT).try_into()?)
            .context("failed to create VMO")?;
        let cli = ethernet::Client::new(
            eth.into_proxy().context("failed to make device proxy")?,
            vmo,
            BUF_SIZE,
            "netdump-test",
        )
        .await
        .context("failed to create ethernet client")?;
        let () = cli.start().await.context("failed to start ethernet client")?;
        Ok(cli)
    }
}

// A simple test of bad arguments where packet capture exits immediately.
#[fasync::run_singlethreaded(test)]
async fn bad_args_test() -> Result {
    let env = TestEnvironment::new("bad_args_test").context("failed to create test environment")?;
    let test_bad_args = |args: Vec<String>| async {
        let (app, _directory_request) =
            env.launch_netdump(args, None).context("failed to launch")?;
        let output = app.await.context("failed to observe clean exit")?;
        assert!(output.ok().is_err());
        Result::Ok(())
    };
    // Empty arguments.
    let () = test_bad_args(Args::empty().into()).await.context("empty arguments")?;
    // Bad device.
    let () = test_bad_args(Args::new_with_device("..").into()).await.context("bad device")?;
    // Device should be last.
    let () = test_bad_args(vec!["some_device".to_string(), "-v".to_string()])
        .await
        .context("bad order")?;
    // Unknown argument.
    let () = test_bad_args(vec!["-x".to_string()]).await.context("invalid argument")?;

    Ok(())
}

// Tests that capturing on Rx path works.
#[fasync::run_singlethreaded(test)]
async fn test_capture_rx() -> Result {
    const PAYLOAD_SIZE: usize = 100;
    let env =
        TestEnvironment::new("test_capture_rx").context("failed to create test environment")?;
    let net = env.create_endpoint().await.context("failed to prepare endpoint")?;
    let fake_ep = net.create_fake_endpoint().context("failed to create fake endpoint")?;

    let args = Args::new().insert_packet_count(PACKET_COUNT);

    let (output, _dumpfile) = env
        .run_netdump(args.into(), || async {
            receive_udp_packets::<net_types::ip::Ipv4>(&fake_ep, PACKET_COUNT, PAYLOAD_SIZE).await
        })
        .await
        .context("run netdump failed")?;
    let () = output.ok().context("netdump finished with error")?;
    let stdout = String::from_utf8_lossy(&output.stdout[..]);
    assert_eq!(stdout.lines().count(), PACKET_COUNT);
    for line in stdout.lines() {
        assert_eq!(line, DEFAULT_UDP_IPV4_OUTPUT)
    }
    Ok(())
}

// Tests that capturing on Tx path works.
#[fasync::run_singlethreaded(test)]
async fn test_capture_tx() -> Result {
    const PAYLOAD_SIZE: usize = 100;
    let env =
        TestEnvironment::new("test_capture_tx").context("failed to create test environment")?;
    let net = env.create_endpoint().await.context("failed to prepare endpoint")?;

    let args = Args::new().insert_packet_count(PACKET_COUNT);
    let mut eth = net.get_ethernet_client().await.context("failed to get ethernet client")?;
    let udp_packet =
        build_udp_packet::<net_types::ip::Ipv4>(PAYLOAD_SIZE, Some(SRC_PORT), DST_PORT);
    let (output, _dumpfile) = env
        .run_netdump(args.into(), || async {
            for _ in 0..PACKET_COUNT {
                let () = eth.send(&udp_packet[..]);
            }
            let sent = futures::future::poll_fn(|ctx| eth.poll_queue_tx(ctx))
                .await
                .context("failed to commit buffers to ethernet client")?;
            // Ensure that the ethernet client sent all the packets.
            assert_eq!(sent, PACKET_COUNT);
            Result::Ok(())
        })
        .await
        .context("run netdump failed")?;
    let () = output.ok().context("netdump finished with error")?;
    let stdout = String::from_utf8_lossy(&output.stdout[..]);
    assert_eq!(stdout.lines().count(), PACKET_COUNT);
    for line in stdout.lines() {
        assert_eq!(line, DEFAULT_UDP_IPV4_OUTPUT)
    }
    Ok(())
}

// When no packets are captured, the section header and interface description blocks should still
// be written.
#[fasync::run_singlethreaded(test)]
async fn pcapng_no_packets_test() -> Result {
    let env = TestEnvironment::new("pcapng_no_packets_test")
        .context("failed to create test environment")?;
    let _net = env.create_endpoint().await.context("failed to prepare endpoint")?;

    let args = Args::new()
        .insert_packet_count(0)
        .insert_pcapng_dump_to_stdout()
        .insert_write_to_dumpfile();

    let (output, dumpfile) = env
        .run_netdump(args.into(), || futures::future::ok(()))
        .await
        .context("run netdump failed")?;
    let () = output.ok().context("netdump finished with error")?;

    assert_eq!(dumpfile, output.stdout);

    assert_eq!(
        dumpfile.len(),
        pcapng::SECTION_HEADER_LENGTH + pcapng::INTERFACE_DESCRIPTION_LENGTH
    );
    assert_eq!(dumpfile, output.stdout);

    let mut buf = dumpfile.as_slice();
    let buf_mut = pcapng::consume_shb_idb(&mut buf)?;
    // All bytes are consumed.
    assert!(buf_mut.is_empty());

    Ok(())
}

// Tests that packet headers are captured and output in PCAPNG format.
// Only one correct packet is necessary for the test to pass.
// However the overall format of the dumpfile is still checked to be valid.
#[fasync::run_singlethreaded(test)]
async fn pcapng_packet_headers_test() -> Result {
    let env = TestEnvironment::new("pcapng_packet_headers_test")
        .context("failed to create test environment")?;
    let net = env.create_endpoint().await.context("failed to prepare endpoint")?;

    const PAYLOAD_LENGTH: usize = 100;

    let args = Args::new()
        .insert_packet_count(PACKET_COUNT)
        .insert_pcapng_dump_to_stdout()
        .insert_write_to_dumpfile();
    let fake_ep = net.create_fake_endpoint().context("failed to create fake endpoint")?;

    let (output, dumpfile) = env
        .run_netdump(args.into(), || async {
            receive_udp_packets::<net_types::ip::Ipv4>(&fake_ep, PACKET_COUNT, PAYLOAD_LENGTH).await
        })
        .await
        .context("run netdump failed")?;
    let () = output.ok().context("netdump finished with error")?;

    assert_eq!(dumpfile, output.stdout);

    let mut buf = dumpfile.as_slice();
    let mut buf_mut = pcapng::consume_shb_idb(&mut buf)?;

    for _ in 0..PACKET_COUNT {
        let (new_buf_mut, packet, packet_length) = pcapng::parse_simple_packet(buf_mut)?;
        let mut packet = &packet[..packet_length];
        buf_mut = new_buf_mut;

        let ethernet = packet_formats::ethernet::EthernetFrame::parse(
            &mut packet,
            packet_formats::ethernet::EthernetFrameLengthCheck::NoCheck,
        )
        .context("failed to parse ethernet")?;
        assert_eq!(ethernet.src_mac().bytes(), &SRC_MAC_BYTES[..]);
        assert_eq!(ethernet.dst_mac().bytes(), &DST_MAC_BYTES[..]);
        assert_eq!(ethernet.ethertype(), Some(packet_formats::ethernet::EtherType::Ipv4));

        let ip = packet_formats::ipv4::Ipv4Packet::parse(&mut packet, ())
            .context("failed to parse IP frame")?;
        assert_eq!(ip.src_ip(), net_types::ip::Ipv4::src_ip());
        assert_eq!(ip.dst_ip(), net_types::ip::Ipv4::dst_ip());
        assert_eq!(ip.proto(), packet_formats::ip::IpProto::Udp);

        let parse_args = packet_formats::udp::UdpParseArgs::new(ip.src_ip(), ip.dst_ip());
        let udp = packet_formats::udp::UdpPacket::parse(&mut packet, parse_args)
            .context("failed to parse UDP")?;
        assert_eq!(udp.src_port(), Some(SRC_PORT));
        // DST_PORT is not zero we can unwrap.
        assert_eq!(udp.dst_port(), DST_PORT);

        // All the payload is there.
        assert_eq!(packet.len(), PAYLOAD_LENGTH);
        // Payoad is all zeros.
        assert!(
            packet.iter().all(|b| *b == 0),
            format!("payload should be all zeroes, got: {:?}", packet)
        );
    }

    assert!((buf_mut.is_empty()));

    Ok(())
}

// Tests that netdump is able to capture and dump malformed packets.
#[fasync::run_singlethreaded(test)]
async fn pcapng_fake_packets_test() -> Result {
    const FAKE_DATA: &str = "this is a badly malformed pack";
    const FAKE_DATA_LENGTH: usize = 30;

    let env = TestEnvironment::new("pcapng_fake_packets_test")
        .context("failed to create test environment")?;
    let net = env.create_endpoint().await.context("failed to prepare endpoint")?;

    let args = Args::new()
        .insert_packet_count(PACKET_COUNT)
        .insert_pcapng_dump_to_stdout()
        .insert_write_to_dumpfile();

    let fake_ep = net.create_fake_endpoint().context("failed to create fake endpoint")?;

    let send_fake_packets = move || {
        futures::stream::repeat(())
            .take(PACKET_COUNT)
            .map(Ok)
            .try_for_each_concurrent(None, move |()| {
                fake_ep.write(FAKE_DATA.as_bytes()).map_err(anyhow::Error::from)
            })
    };

    let (output, dumpfile) =
        env.run_netdump(args.into(), send_fake_packets).await.context("run netdump failed")?;
    let () = output.ok().context("netdump finished with error")?;

    assert_eq!(dumpfile, output.stdout);

    let mut buf = dumpfile.as_slice();
    let mut buf_mut = pcapng::consume_shb_idb(&mut buf)?;

    for _ in 0..PACKET_COUNT {
        let (new_buf_mut, packet, packet_length) = pcapng::parse_simple_packet(buf_mut)?;
        buf_mut = new_buf_mut;
        // Packet length is still the original length.
        assert_eq!(packet_length, FAKE_DATA_LENGTH);
        // But data should be padded to multiple of 4 octets.
        assert_eq!(
            String::from_utf8_lossy(packet),
            "this is a badly malformed pack\0\0".to_string()
        );
    }

    assert!((buf_mut.is_empty()));
    Ok(())
}

// A test that specifying a timeout causes netdump to quit by itself.
// The timeout is only intended to be best-effort. Capture exiting successfully
// is the only criterion for the test to pass.
#[fasync::run_singlethreaded(test)]
async fn timeout_test() -> Result {
    let env = TestEnvironment::new("timeout_test").context("failed to create test environment")?;
    let _net = env.create_endpoint().await.context("failed to prepare endpoint")?;

    let args = Args::new().insert_timeout(0);
    let (app, _dir_request) =
        env.launch_netdump(args.into(), None).context("failed to launch netdump")?;
    let output = app.await.context("run netdump failed")?;
    let () = output.ok().context("netdump finished with error")?;

    let args = Args::new().insert_timeout(1);
    let (app, _dir_request) =
        env.launch_netdump(args.into(), None).context("failed to launch netdump")?;
    let output = app.await.context("run netdump failed")?;
    let () = output.ok().context("netdump finished with error")?;

    Ok(())
}

// A test of bad filter strings where packet capture exits immediately.
// Not intending to test every syntax error as that should be unit tested.
#[fasync::run_singlethreaded(test)]
async fn bad_filters_test() -> Result {
    let env =
        TestEnvironment::new("bad_filters_test").context("failed to create test environment")?;
    let _net = env.create_endpoint().await.context("failed to prepare endpoint")?;

    let run_with_filter = |filter: &str| {
        let args = Args::new().insert_filter(filter);
        async {
            let (app, _directory_request) =
                env.launch_netdump(args.into(), None).context("failed to launch netdump")?;
            let output = app.await.context("run netdump failed")?;
            assert!(output.ok().is_err());
            Result::Ok(())
        }
    };

    // Empty filter string.
    let () = run_with_filter("").await?;
    // Invalid keyword.
    let () = run_with_filter("this is magic").await?;
    // Invalid parenthesis.
    let () = run_with_filter("udp and ( ip4 tcp port 80").await?;
    // Invalid port name.
    let () = run_with_filter("udp and ip4 tcp port htp").await?;

    Ok(())
}

/// Helper to run filter tests.
///
/// `name` is the test name, `filter_str` is the filter argument passed to
/// netdump, and `packets` is a list of packets, each of which will be received
/// by netdump [`PACKET_COUNT`] times.
///
/// On success, returns the captured stdout from netdump.
async fn run_filter_test(name: &str, filter_str: &str, packets: &[Vec<u8>]) -> Result<Vec<u8>> {
    let env = TestEnvironment::new(name).context("failed to create test environment")?;
    let net = env.create_endpoint().await.context("failed to prepare endpoint")?;

    let args = Args::new().insert_packet_count(PACKET_COUNT).insert_filter(filter_str);

    let fake_ep = net.create_fake_endpoint().context("failed to create fake endpoint")?;

    let send_packets_fut = || {
        async {
            for _ in 0..PACKET_COUNT {
                // Interleave test packets up to PACKET_COUNT.
                for p in packets {
                    let () = fake_ep.write(&p[..]).await.context("failed to write to endpoint")?;
                }
            }
            Ok(())
        }
    };
    let (output, _dumpfile) =
        env.run_netdump(args.into(), send_packets_fut).await.context("run netdump failed")?;
    let () = output.ok().context("netdump finished with error")?;
    Ok(output.stdout)
}

// Test the effect of a positive filter (no leading "not") on packet capture.
// Should expect only the packets accepted by the filter are captured.
#[fasync::run_singlethreaded(test)]
async fn positive_filter_test() -> Result {
    const PAYLOAD_SIZE: usize = 100;
    const BIG_PAYLOAD_SIZE: usize = 1000;
    let filter = format!("greater {}", BIG_PAYLOAD_SIZE);
    let short_packet =
        build_udp_packet::<net_types::ip::Ipv4>(PAYLOAD_SIZE, Some(SRC_PORT), DST_PORT);
    let long_packet =
        build_udp_packet::<net_types::ip::Ipv4>(BIG_PAYLOAD_SIZE, Some(SRC_PORT), DST_PORT);
    let stdout = run_filter_test("positive_filter_test", &filter, &[short_packet, long_packet][..])
        .await
        .context("failed to get output")?;
    let stdout = String::from_utf8_lossy(&stdout[..]);
    let packets: Vec<String> = stdout.lines().map(String::from).collect();
    assert_eq!(packets.len(), PACKET_COUNT);
    // Expected data.
    let exp_length = format!("{}", 20 + 8 + BIG_PAYLOAD_SIZE); // IPv4 + UDP + Payload.
    for packet in packets {
        if !packet.contains(&exp_length) {
            return Err(anyhow::anyhow!("Unfiltered packet found! {}", packet));
        }
    }
    Ok(())
}

// Test the effect of a negative filter in packet capture.
// Should expect only the packets accepted by the filter are captured.
#[fasync::run_singlethreaded(test)]
async fn negative_filter_test() -> Result {
    const ALT_PORT: std::num::NonZeroU16 = unsafe { std::num::NonZeroU16::new_unchecked(3333) };
    const PAYLOAD_SIZE: usize = 100;
    let filter = format!("not port {}", ALT_PORT);
    let default_ports_packet =
        build_udp_packet::<net_types::ip::Ipv4>(PAYLOAD_SIZE, Some(SRC_PORT), DST_PORT);
    let alt_port_packet =
        build_udp_packet::<net_types::ip::Ipv4>(PAYLOAD_SIZE, Some(ALT_PORT), DST_PORT);
    let stdout = run_filter_test(
        "negative_filter_test",
        &filter,
        &[default_ports_packet, alt_port_packet][..],
    )
    .await
    .context("failed to get output")?;
    let stdout = String::from_utf8_lossy(&stdout[..]);
    let packets: Vec<String> = stdout.lines().map(String::from).collect();
    assert_eq!(packets.len(), PACKET_COUNT);

    let alt_port_str = format!("{}", ALT_PORT);
    for packet in packets {
        if packet.contains(&alt_port_str) {
            return Err(anyhow::anyhow!("Unfiltered packet found! {}", packet));
        }
    }

    Ok(())
}

// Test that a long filter string works.
// Only testing a subcomponent of the filter.
#[fasync::run_singlethreaded(test)]
async fn long_filter_string_test() -> Result {
    const PAYLOAD_SIZE: usize = 100;
    const ALT_PORT: std::num::NonZeroU16 = unsafe { std::num::NonZeroU16::new_unchecked(2345) };
    let filter = format!(
        "not ( port 65026,65268,ssh or dst port 8083 or ip6 dst port 33330-33341 or proto udp dst port {} or ip4 udp dst port 1900 )",
        ALT_PORT
    );
    let default_ports_packet =
        build_udp_packet::<net_types::ip::Ipv4>(PAYLOAD_SIZE, Some(SRC_PORT), DST_PORT);
    let alt_port_packet =
        build_udp_packet::<net_types::ip::Ipv4>(PAYLOAD_SIZE, Some(SRC_PORT), ALT_PORT);
    let stdout = run_filter_test(
        "long_filter_string_test",
        &filter,
        &[default_ports_packet, alt_port_packet][..],
    )
    .await
    .context("failed to get output")?;
    let stdout = String::from_utf8_lossy(&stdout[..]);
    let packets: Vec<String> = stdout.lines().map(String::from).collect();
    assert_eq!(packets.len(), PACKET_COUNT);

    let alt_port_str = format!("{}", ALT_PORT);
    for packet in packets {
        if packet.contains(&alt_port_str) {
            return Err(anyhow::anyhow!("Unfiltered packet found! {}", packet));
        }
    }

    Ok(())
}
