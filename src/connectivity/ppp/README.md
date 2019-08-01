# Point-to-Point Protocol

Drivers, libraries, services, and tools providing Point-to-Point Protocol support on Fuchsia.
Provides a direct link-layer over a serial connection, with support for IPv4 and IPv6 network
layers.

## Definitions
- PPP: Point-to-Point Protocol.
- LCP: Link Control Protocol.
- IPCP: IPv4 Control Protocol
- IPV6CP: IPv6 Control Protocol.

## Source Layout
- Drivers
  - Serial PPP: A driver which binds to a serial
  driver and provides muxing and de-muxing of PPP protocols, and framing and de-framing on
  `Read`/`Write`.
- Libraries
  - Common: Common protocol number definitions.
  - HDLC: Frame check sequence calculation and HDLC-like framing and de-framing functions.
  - PPP Packet: Parsing and serialization of various PPP packet types, including control protocols
  and their options.
  - PPP Protocol: Generic implementation of the PPP control protocol state machine. Includes
  specialized implementations for LCP, IPCP and IPV6CP.
- Services
  - PPP Server: Configure and maintain single connections between a remote peer and local PPP client application. Provides an interface for local applications to open and close PPP connections.
- Tools
  - PPP CLI: Parse commands and options and forward them as commands to `PPP Server` to open and close connections. Does not maintain any state about the connection and does not stay alive after confirming a single command response.

## SDK
- [fuchsia.net.ppp](../../../sdk/fidl/fuchsia.net.ppp) provides both device and service interfaces.

## Build
Main build targets are defined in the root `BUILD.gn`. `:ppp` builds all drivers, services, and tools. `:tests` builds all tests.

## Test
- `% fx run-test ppp-tests`: Runs Serial PPP tests, and HDLC tests.
- `% fx run-test ppp-packet-tests`: Runs PPP Packet tests.
- `% fx run-test ppp-protocol-tests`: Runs PPP Protocol tests.

## Example IPv4 Configuration with QEMU and PPPD on Linux
Host commands begin with `%`, client commands begin with `$`, QEMU monitor commands begin with `(qemu) `.
- Ensure you have built with PPP support (see above).
- `% fx run -N -- -serial mon:stdio -serial pty`.
    - This command tells QEMU to multiplex monitor and serial port 0 (kernel serial log) on
    stdin/out.
    - Optionally, the `kernel.serial=none` option can be given to allow PPP access to serial port 0.
- Switch to monitor mode (default in QEMU is `C-a c`).
- `(qemu) info chardev`. Note the pty for `serial1` (refered to as `PTY` below).
- Switch back to console mode.
- `$ run ppp-cli open -4 IP_ADDRESS -d /dev/class/ppp/000` begins client handshake to bring up IPv4
with some IP address `IP_ADDRESS` on `serial1`.
- `% sudo /sbin/pppd nodetach debug noauth PTY` brings up a PPP connection with IPv4 on the host.
    - `nodetach` is optional, but makes it significantly easier to end the connection.
    - Optionally, `debug` can be passed to get a verbose log of the packets being sent and received.
- Upon successful connection, `pppd` will provide a network interface (commonly `ppp0`) and the remote and local IP addresses that were negotiated.
- The client will also have a network interface `ppp#`. To bring it up for use with the netstack, run `ifconfig ppp# add IP_ADDRESS/MASK` followed by `ifconfig ppp# up`.
- The host and device network interfaces are now ready for use. This can be verified by pinging the client's IP from the host.
