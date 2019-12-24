// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helper library for integration testing netdump.

use {
    anyhow::{format_err, Error},
    fdio::{self, WatchEvent},
    fidl_fuchsia_netemul_environment::ManagedEnvironmentMarker,
    fidl_fuchsia_netemul_network::{
        EndpointManagerMarker, EndpointManagerProxy, EndpointProxy, FakeEndpointMarker,
        FakeEndpointProxy, NetworkContextMarker, NetworkManagerMarker, NetworkProxy,
    },
    fidl_fuchsia_sys::{LauncherMarker, LauncherProxy},
    fuchsia_async::{Executor, Interval},
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::{
        future::{self, Either},
        Future, StreamExt,
    },
    net_types::ip::IpVersion,
    std::boxed::Box,
    std::fs::File,
    std::net::{IpAddr, SocketAddr, UdpSocket},
    std::path::Path,
};

mod tests;

/// Directory for dumpfile to reside.
pub const DUMPFILE_DIR: &str = "/vdata";
/// Default dumpfile name to use.
pub const DEFAULT_DUMPFILE: &str = "netdump.pcapng";
/// Minimum valid dumpfile size in bytes.
/// Size of PCAPNG section header + interface description blocks.
pub const MIN_DUMPFILE_SIZE: usize = 28 + 20;

const DUMPFILE_SIZE_CHECK_INTERVAL_MILLIS: i64 = 30;

const NETDUMP_URL: &str = fuchsia_component::fuchsia_single_component_package_url!("netdump");

/// Helper for constructing valid command-line arguments to netdump.
pub struct Args {
    // Always non-empty with the device path being the last element.
    args: Vec<String>,
}

impl From<Args> for Vec<String> {
    fn from(input: Args) -> Self {
        input.args
    }
}

impl Args {
    /// New set of arguments given an endpoint path of the device to sniff.
    pub fn new(path: &str) -> Self {
        Args { args: vec![path.to_string()] }
    }

    /// Insert an argument in the correct place in the argument list before the capture device path.
    pub fn insert_arg(mut self, arg: String) -> Self {
        let last = self.args.len() - 1;
        self.args.insert(last, arg);
        self
    }

    /// Helper method for inserting a packet count argument.
    pub fn insert_packet_count(self, count: usize) -> Self {
        self.insert_arg("-c".into()).insert_arg(count.to_string())
    }

    /// Helper method for inserting a display link-level information argument.
    pub fn insert_link_level(self) -> Self {
        self.insert_arg("-e".into())
    }

    /// Helper method for inserting a filter argument.
    pub fn insert_filter(self, filter: &str) -> Self {
        self.insert_arg("-f".into()).insert_arg(filter.into())
    }

    /// Helper method for inserting a timeout argument.
    pub fn insert_timeout(self, timeout_secs: u64) -> Self {
        self.insert_arg("-t".into()).insert_arg(timeout_secs.to_string())
    }

    /// Helper method for inserting a write to pcapng dumpfile argument.
    /// `path`: The file path to use, relative to `DUMPFILE_DIR`.
    pub fn insert_write_to_dumpfile(self, path: &str) -> Self {
        self.insert_arg("-w".into()).insert_arg(format!("{}/{}", DUMPFILE_DIR, path))
    }

    /// Helper method for inserting a pcapng dump to stdout argument.
    pub fn insert_pcapng_dump_to_stdout(self) -> Self {
        self.insert_arg("--pcapdump".into())
    }
}

#[derive(Copy, Clone)]
pub enum EndpointType {
    TX,
    RX,
}

/// Provide default values for creating a socket attached to the endpoint.
impl EndpointType {
    pub fn default_port(self) -> u16 {
        match self {
            Self::TX => 1111,
            Self::RX => 2222,
        }
    }

    pub fn default_ip_addr_str(self, ver: IpVersion) -> &'static str {
        match (ver, self) {
            (IpVersion::V4, EndpointType::TX) => "192.168.0.1",
            (IpVersion::V6, EndpointType::TX) => "fd00::1",
            (IpVersion::V4, EndpointType::RX) => "192.168.0.2",
            (IpVersion::V6, EndpointType::RX) => "fd00::2",
        }
    }

    pub fn default_ip_addr(self, ver: IpVersion) -> IpAddr {
        self.default_ip_addr_str(ver).parse().unwrap()
    }

    pub fn default_socket_addr(self, ver: IpVersion) -> SocketAddr {
        SocketAddr::new(self.default_ip_addr(ver), self.default_port())
    }
}

/// The environment infrastructure for a test consisting of multiple test cases, for helping with
/// launching packet capture. Only one should be created per test. It is expected that the running
/// test `test_name` has the following configured as part of the netemul runner setup:
/// - Endpoints `test_name_tx` and `test_name_rx`.
/// - A network `test_name_net` with the above endpoints present.
/// - The endpoints are attached to the ambient `ManagedEnvironment` under `/vdev/class/ethernet/`.
/// Packets are isolated to each network, and do not travel across networks.
/// The "tx" and "rx" in the endpoint names are to suggest the source and sink endpoints for packets
/// in tests. However there is no actual restriction on use.
///
/// There are multiple ways to write packets to the network:
/// - The backing Ethernet device for the endpoints can be obtained for writing.
/// - The endpoints can be bound to netstack, which is in turn exercised to produce packets.
/// - A netemul `FakeEndpoint` can be used to write to the network directly.
/// To ensure that packet capturing and writing to the network happen concurrently, tests should
/// set up a Fuchsia async `Executor` and give its ownership to `TestEnvironment`. This allows
/// execution of async test case code uniformly through the provided `run_test_case` method.
/// TODO(CONN-170): Allow parallel test cases.
pub struct TestEnvironment {
    test_name: String,
    epm: EndpointManagerProxy,
    net: NetworkProxy,
    launcher: LauncherProxy,
    executor: Executor,
}

impl TestEnvironment {
    pub fn new(mut executor: Executor, test_name: &str) -> Result<Self, Error> {
        let env = client::connect_to_service::<ManagedEnvironmentMarker>()?;
        let netctx = client::connect_to_service::<NetworkContextMarker>()?;

        let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
        netctx.get_endpoint_manager(epm_server_end)?;
        let (netm, netm_server_end) = fidl::endpoints::create_proxy::<NetworkManagerMarker>()?;
        netctx.get_network_manager(netm_server_end)?;

        let net_name = format!("{}_net", test_name);
        let net = executor
            .run_singlethreaded(netm.get_network(&net_name))?
            .ok_or(format_err!("Cannot find network {}", &net_name))?
            .into_proxy()?;

        let (launcher, launcher_req) = fidl::endpoints::create_proxy::<LauncherMarker>()?;
        env.get_launcher(launcher_req)?;

        Ok(Self { test_name: test_name.to_string(), epm, net, launcher, executor })
    }

    /// Create a new `Args` instance with the specified endpoint as the capture device.
    pub fn new_args(&self, ep_type: EndpointType) -> Args {
        Args::new(&format!("/vdev/class/ethernet/{}", &self.ep_name_by_type(ep_type)))
    }

    /// Run a test case in the current environment. Can be reused for multiple test cases provided
    /// that all previous test cases passed and did not return `Result::Err`.
    ///
    /// Packet capture during the test case will be launched with the given arguments.
    /// Packet transmission is performed concurrently after a watched dumpfile comes into existence.
    /// This method blocks until both packet transmission and capture are complete. No timeout is
    /// implemented, but the test will be killed by the global timeout enforced by netemul.
    /// The result is the output produced by the capture.
    /// `args`: Arguments to launch packet capture.
    /// `send_packets_fut`: The future to execute in order to send packets on the network.
    /// `dumpfile`: The file path relative to `DUMPFILE_DIR` to watch for a dumpfile. If the file
    /// exists, it is removed. Packet transmission begins after the file is added by packet capture
    /// and become non-zero in size due to the output of PCAPNG header blocks.
    pub fn run_test_case(
        &mut self,
        args: Vec<String>,
        send_packets_fut: impl Future<Output = Result<(), Error>> + Send + 'static,
        dumpfile: &str,
    ) -> Result<client::Output, Error> {
        Self::remove_dumpfile(dumpfile)?;

        let app_fut =
            client::AppBuilder::new(NETDUMP_URL.to_string()).args(args).output(&self.launcher)?;

        let watcher_fut = Box::pin(Self::watch_for_dumpfile(dumpfile.into()));
        let setup_fut = future::select(app_fut, watcher_fut);
        // Run capture setup and dumpfile watcher on 2 threads.
        // This should not deadlock as netdump always writes PCAPNG header bocks in its
        // write_shb() and write_idb() calls in handle_rx() to file even if no packets
        // are transmitted.
        let capture_fut = match self.executor.run(setup_fut, 2) {
            Either::Left(_) => Err(format_err!(
                "Capture exited early. Please refer to FLK-487 if this appears flaky."
            )),
            Either::Right((_, fut)) => Ok(fut),
        }?;

        // Complete the test by running capture and packet transmission on 2 threads.
        match self.executor.run(future::try_join(capture_fut, send_packets_fut), 2)? {
            (output, _) => Ok(output),
        }
    }

    /// Run a test case in the current environment that does not require packets on the network.
    /// Can be reused for multiple test cases provided that all previous test cases passed and did not
    /// return `Result::Err`. Useful for testing invalid packet capture setup.
    pub fn run_test_case_no_packets(&mut self, args: Vec<String>) -> Result<client::Output, Error> {
        let app_fut =
            client::AppBuilder::new(NETDUMP_URL.to_string()).args(args).output(&self.launcher)?;

        self.executor.run_singlethreaded(app_fut)
    }

    /// Get one of the real endpoints in the environment.
    pub fn endpoint(&mut self, ep_type: EndpointType) -> Result<EndpointProxy, Error> {
        let ep_name = self.ep_name_by_type(ep_type);
        let ep = self
            .executor
            .run_singlethreaded(self.epm.get_endpoint(&ep_name))?
            .ok_or(format_err!("Cannot obtain endpoint {}", &ep_name))?
            .into_proxy()?;
        Ok(ep)
    }

    /// Create a new fake endpoint.
    pub fn create_fake_endpoint(&self) -> Result<FakeEndpointProxy, Error> {
        let (ep_fake, ep_fake_server_end) = fidl::endpoints::create_proxy::<FakeEndpointMarker>()?;
        self.net.create_fake_endpoint(ep_fake_server_end)?;
        Ok(ep_fake)
    }

    fn ep_name_by_type(&self, ep_type: EndpointType) -> String {
        match ep_type {
            EndpointType::TX => format!("{}_tx", &self.test_name),
            EndpointType::RX => format!("{}_rx", &self.test_name),
        }
    }

    // Remove the dumpfile if it exists.
    fn remove_dumpfile(dumpfile: &str) -> Result<(), Error> {
        let dumpfile_path_string = format!("{}/{}", DUMPFILE_DIR, dumpfile);
        let dumpfile_path = Path::new(&dumpfile_path_string);
        if dumpfile_path.exists() {
            std::fs::remove_file(dumpfile_path)?;
        }
        Ok(())
    }

    // Watch for the dumpfile to be added, then periodically check its size.
    async fn watch_for_dumpfile(dumpfile: String) -> Result<(), Error> {
        let dir = File::open(DUMPFILE_DIR)?;
        let dumpfile_path = Path::new(&dumpfile);
        // Blocks until the relevant `AddFile` event is received.
        let status = fdio::watch_directory(&dir, zx::sys::ZX_TIME_INFINITE, |event, got_path| {
            if event == WatchEvent::AddFile && got_path == dumpfile_path {
                Err(zx::Status::STOP)
            } else {
                Ok(()) // Continue watching.
            }
        });
        match status {
            zx::Status::STOP => Ok(()), // Dumpfile now exists.
            status => Err(format_err!("Dumpfile watcher returned with status {:?}", status)),
        }?;

        let fut = Self::watch_dumpfile_size(format!("{}/{}", DUMPFILE_DIR, &dumpfile));
        fut.await
    }

    // Periodically check for dumpfile size until it is at least minimum size.
    async fn watch_dumpfile_size(dumpfile_path_str: String) -> Result<(), Error> {
        let interval_dur = zx::Duration::from_millis(DUMPFILE_SIZE_CHECK_INTERVAL_MILLIS);
        let mut interval = Interval::new(interval_dur);
        let dumpfile_path = Path::new(&dumpfile_path_str);
        while std::fs::metadata(&dumpfile_path)?.len() < MIN_DUMPFILE_SIZE as u64 {
            interval.next().await;
        }
        Ok(())
    }
}

/// Tear down `TestEnvironment` and return the owned executor.
impl From<TestEnvironment> for Executor {
    fn from(env: TestEnvironment) -> Self {
        env.executor
    }
}

#[macro_export]
macro_rules! test_case {
    ($name:ident, $($args:expr),*) => {
        println!("Running test case {}.", stringify!($name));
        $name($($args),*)?;
        println!("Test case {} passed.", stringify!($name));
    };
}

/// Converting the `client::Output` data of components into a String.
pub fn output_string(data: &Vec<u8>) -> String {
    String::from_utf8_lossy(data).into_owned()
}

/// Return Ok if everything in `substrs` is a substring of `st`, otherwise Err with the substring
/// that is not present.
pub fn check_all_substrs<'a>(st: &str, substrs: &[&'a str]) -> Result<(), &'a str> {
    substrs.iter().try_for_each(|&substr| if st.contains(substr) { Ok(()) } else { Err(substr) })
}

/// Example packet transmission task that can be used with `TestEnvironment::run_test_case`.
/// Send UDP packets from the given socket to the receiving socket address `addr_rx`.
pub async fn send_udp_packets(
    socket: UdpSocket,
    addr_rx: SocketAddr,
    payload_size_octets: usize,
    count: usize,
) -> Result<(), Error> {
    let buf: Vec<u8> = std::iter::repeat(0).take(payload_size_octets).collect();
    for _ in 0..count {
        socket.send_to(&buf, addr_rx)?;
    }
    Ok(())
}
