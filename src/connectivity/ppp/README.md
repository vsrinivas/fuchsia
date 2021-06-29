  # Point-to-Point Protocol

Drivers, libraries, services, and tools providing Point-to-Point Protocol
support on Fuchsia. PPP provides a direct link-layer (L2) over a serial
connection. It supports IPv4 and IPv6 network layers.

## Definitions
- PPP: Point-to-Point Protocol.
- LCP: Link Control Protocol.
- IPCP: IPv4 Control Protocol
- IPV6CP: IPv6 Control Protocol.

## Source Layout
- Drivers
  - Serial PPP: Reads and writes frames over a bound serial port. Also provides
    muxing of PPP protocols.
- Libraries
  - Common: Provides common protocol number definitions.
  - HDLC: Provides frame check sequence calculation and HDLC-like framing
    functions.
  - PPP Packet: Provides serialization of packet types, including control
    protocols and their options.
  - PPP Protocol: Provides a generic implementation of the PPP control protocol
    state machine. Also includes implementations of LCP, IPCP and IPV6CP.
- Services
  - PPP Server: Configures and maintains one PPP connection. Provides an
    interface for local applications to open and close PPP connections.
- Tools
  - PPP CLI: Parses commands and forwards them to `PPP Server` to open and close
    connections. Does not maintain any state about the connection. Exits after
    sending one command.

## SDK
- [fuchsia.net.ppp](../../../sdk/fidl/fuchsia.net.ppp) provides both device and
  service interfaces.

## Build
The root `BUILD.gn` defines the main build targets. `:ppp` builds all drivers,
services, and tools. `:tests` builds all tests.

## Test
- `% fx test ppp-tests`: Runs Serial PPP tests, and HDLC tests.
- `% fx test ppp-packet-tests`: Runs PPP Packet tests.
- `% fx test ppp-protocol-tests`: Runs PPP Protocol tests.

## Example IPv4 Configuration with QEMU and PPPD on Linux
Host commands begin with `%`. Client commands begin with `$`. QEMU monitor
commands begin with `(qemu)`.
- Ensure you have built with PPP support (see above).
- `% fx qemu -N -- -serial mon:stdio -serial pty`.
    - This tells QEMU to multiplex monitor and serial port 0 (kernel serial log)
      on stdin/out.
    - Provide `kernel.serial=none` to allow PPP access to serial port 0.
- Switch to monitor mode (default in QEMU is `C-a c`).
- `(qemu) info chardev`.
    - Note the pty for `serial1` (referred to as `PTY` below).
- Switch back to console mode.
- `$ run ppp-cli open -4 IP_ADDRESS -d /dev/class/ppp/000`
    - This tells the server to try to bring up IPv4 with some IP address
      `IP_ADDRESS` on `serial1`.
- `% sudo /sbin/pppd nodetach debug noauth PTY`
    - This brings up a PPP connection with IPv4 on the host with the given pty.
    - Pass `nodetach` to make ending the connection easier.
    - Pass `debug` to get a verbose log of the send and received packets.
    - Pass `noauth` because the client does not yet support authentication.
- On success, `pppd` provides a network interface (`ppp0`) and the negotiated IP
  addresses.
- The client will also have a network interface `ppp#`. To bring it up for use
  with the netstack, run `ip addr add dev ppp# IP_ADDRESS/MASK` followed by
  `ip link set ppp# up`.
- The host and device network interfaces are ready for use. You can verify this
  by pinging the client's IP from the host.

# Design
## `fuchsia.net.ppp`
The `Device` protocol focuses on a simple `Tx`/`Rx` interface. It relies on the
caller to provide a protocol with each message. `Tx` and `Rx` follow a hanging
get pattern, blocking until the driver has sent or received the data. This
allows natural backpressure and avoids signalling clients. `Enable` controls
whether the driver has exclusive access to the serial port. `SetStatus`/
`GetStatus` control the allowed network protocols for the current PPP
connection.

## `serial-ppp`
The driver reads and writes PPP frames according to the HDLC-like framing in
RFC-1662. It also muxes the different protocols between driver clients. The
driver communicates with clients using the `fuchsia.net.ppp` FIDL.

The HDLC framing, and Frame Check Sequence calculation are in a library for
reuse.

To read frames and respond to FIDL messages, the driver maintains two threads.
One thread runs an async loop, and binds to each incoming client. It loops and
reads FIDL messages, dispatching them to the driver. The other thread is the
reader thread. It reads data from the serial port and responds to callbacks when
packets are available.

The driver maintains a fixed-size queue and a callback for each protocol a
client can request. When the driver receives an `Rx` call, it registers a
callback to reply for the specified protocol.

The reader loop checks if there are any registered callbacks it has packets for.
If it has a packet and a callback, it pops the packet from its queue, replies,
and deregisters the callback. Once the loop has finished trying each callback,
it attempts to read one packet from the serial port. The loop will read bytes
one-by-one, scanning for the end of frame byte. (The serial port is far too slow
for this to cause a bottleneck.) It will read up to the max frame size, and will
time out if reading the next byte takes too long. In either case, it discards
the malformed packet. In case it read a full packet but had invalid format or
frame check sequence, it discards the packet.

If the driver has a valid packet, it will inspect the packet protocol and push
it into the correct queue. Each queue drops its oldest packets to maintain a
fixed max size.

On a call to `Tx`, the driver will block the reply until it has framed and
written all data to the serial port.

The driver is thread-safe and can handle more than one in-flight FIDL message at
the same time.

## `ppp_packet`
The packet library allows encoding PPP packet formats in the type system. It
also provides convenient serialization of packets. It depends on the `packet`
crate and the `zerocopy` crate, which manipulate buffers. The library provides a
high level interface to specify packet structure.

The library supports serializing and parsing the configuration options for each
protocol. Control protocols encode options as Type-Length-Value elements. You
can add new LCP-like protocols by defining the options and any extra packet
formats.

## `ppp_protocol`
The protocol library is the heart of the PPP implementation. It contains a
generic implementation of an LCP-like control protocol. It also specializes and
extends this to support LCP, IPCP and IPv6CP. It manipulates packets based on
events given to the control protocol state machine. The state machine uses the
typestate pattern. This allows the compiler to verify correctness.

The protocol library allows extension without duplication of the core protocol
logic. You can add a new control protocol by defining options and how to
interact with them.

The library supports user-defined asynchronous methods for sending and receiving
packets. It also supports configurable timeout logic.

The architecture of the state machine follows the one specified in RFC-1661. It
combines the closed/ closing/down/stopped/stopping states into one closed state.
Transitions handle the missing states. As a first implementation, some features
of PPP are not implemented. Frame compression, authentication, non-default MRU,
and link quality monitoring are not implemented. The architecture of the library
supports adding these in the future if necessary.

## `ppp-cli`
The CLI is a thin wrapper around `Ppp` FIDL's `Open` and `Close`. It parses the
command line to build up a call. It then executes the call with the PPP server
and exits when it receives a reply. It does not keep running while the
connection is open.

## `ppp-server`
The server is a long-lived process that manages one open PPP connection at a
time. Changes to the server could allow more than one simultaneous PPP
connections.

The server asynchronously processes commands, packets, and timeouts. The server
flattens each client into a single stream of commands. (The server has no need
to distinguish between clients, as the CLI exits after one command). When no
connection is open, the server listens for commands from clients.

On `Open`, the server takes options from the message. It begins giving timeout
and packets events to the state machine. When a network protocol opens, it
signals the driver. The server stays in a loop waiting on more commands,
packets, or timeouts. On any command, the server closes the connection. The
server resumes the command processing loop.
