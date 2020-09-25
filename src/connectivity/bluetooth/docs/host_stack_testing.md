# Testing utility reference

This document provides a central location for all of the test utilities
available to `bt-host` unit test developers. Many of these are test doubles of
commonly used `bt-host` objects, but there are a variety of other test
utilities.

**Note:{#test-loop-harness}** There's one non test-double pattern that pops up
in a few places in bt-host: a **test harness** which inherits from the
asynchronous dispatcher loop GTest fixture and instantiates a useful test double
member. This test harness provides an accessor to the test double and
(sometimes) other useful methods on top of the test double. Test fixtures for
layers dependent on $test_double can then inherit from the new harness for extra
convenience.

## Definitions

The bt-host code-base provides an extensive unit test suite and as such it
employs a variety of available test utilities. To better understand the
differences between test utilities, it is recommended to understand the
following terminology:

* **Test code**: Code written to test production code.

    * Note: The `bt-host` C++ codebase uses the convention that for production
            code in `foo.(h|cc)`, associated tests are written in
            `foo_unittest.cc`.

* **Code under test**: The production code tested by **test code**.
* **Test Double**: A generic term for an object used to stand in for some piece
  of production code in tests. Test doubles are often used with dependency
  injection - objects depend only on an interface, and in test code, they are
  constructed with a test double implementing that interface. Test doubles allow
  for isolated (unit) testing of an individual piece of code. Each of the
  following terms is a specific type of test double.
* **Stub**: A test double which provides canned responses to requests from code
  under test. A related type of test double is a test **spy**, which also
  provides canned responses, but allows test code to peek into the requests made
  by the code under test.
* **Mock**: A test double which allows test code to set expectations on the
  behavior of code under test. Once configured by test code, the mock will
  verify that certain calls have been made by the code under test, and possibly
  perform custom behavior based on these calls if configured to do so.
* **Fake**: A test double which implements a realistic, but simplified version
  of a production object. Fakes are often used in place of difficult to
  replicate, expensive, or slow dependencies, e.g. databases or hardware.

Note: For additional information, see
[Test Doubles — Fakes, Mocks and Stubs](https://blog.pragmatists.com/test-doubles-fakes-mocks-and-stubs-1a7491dfa3da).

## Bluetooth controller test utilities

Consider the Core Bluetooth system architecture (i.e. not services or protocols
built on top of the Core Specification). The main three components are the
Bluetooth Controller, which is implemented in firmware and hardware on the
Bluetooth radio device, the Host-Controller-Interface (HCI) protocol, and the
Bluetooth Host, i.e. `bt-host` and the rest of
[the core stack](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/).
One way to test the Host subsystem without using a physical Bluetooth controller
would be to create a Controller test double which responds to HCI commands from
`bt-host`. As the Controller exists “below” the Host subsystem, this is the
lowest-level way to test `bt-host` functionality. Given the complexity of
Bluetooth controller software, there are a variety of test utilities available
for different use cases.


#### [ControllerTestDoubleBase](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/controller_test_double_base.h)

`ControllerTestDoubleBase` exists to provide basic functionality (mostly related
to opening and handing off Bluetooth data channels) needed by any Controller
test double. This class is not used directly, but subclassed by Controller test
doubles for shared functionality.


#### [FakeController](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/fake_controller.h)

`FakeController` is a test fake of the Bluetooth Controller. It emulates the HCI
protocol by employing reasonable default software logic to respond to HCI
commands from `bt-host`. It also provides some data plane emulation.

`FakeController` sees usage in tests of code one or more layers above explicit
HCI commands, where the controller is often an indirect dependency. As such,
`FakeController` uses [`FakePeer`](#fakepeer) along with the
[emulation utilties](#emulation-helpers) outlined below to emulate a full fake
piconet. `FakeController` allows indirect configuration through manipulation of
the fake piconet, such as adding or removing `FakePeer`s. It is a test fake in
the sense that its direct interactions with `bt-host` code, the HCI commands and
events, are not directly configured. Use `FakeController` when you want to
simulate a controller with standard behavior and your code under test does not
rely on specific HCI commands or responses.

##### [FakePeer](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/fake_peer.h)

`FakePeer`, used along with the `FakeController` utility, provides a deep level
of Bluetooth peer device control plane emulation. It aggregates a variety of
lower-level test doubles, such as `FakeL2cap`, `FakeLayer`,
`FakeSignalingServer`, and `FakeSdpServer`. `FakePeer` allows high-level
GAP-level production code to use the `FakeController` and emulate
realistically-behaved remote device. Multiple fake peers can be used to emulate
a full-fledge piconet. Use `FakePeer` in integration-style tests of integration
points between many objects.

#### [MockController](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/mock_controller.h)

`MockController` allows clients to dynamically mock out expected Controller
behavior. Clients queue up specific HCI commands and responses, and
`MockController` verifies that these transactions occur in the expected order.
`MockController` mostly sees usage in code that directly interacts with the HCI
protocol. Use `MockController` to simulate an exact flow of either ACL packets
or HCI commands, or to inject test-specific HCI behavior into a test.


#### [ControllerTest](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/controller_test.h)

`ControllerTest` is not a test double in itself, but a
[test harness of the type described above](#test-loop-harness). `ControllerTest`
is templated over a `ControllerTestDoubleBase`, so clients can access the same
fixture functionality with any concrete `ControllerTestDoubleBase` implementor
they provide. Test suites that depend on test double Controllers tend to
subclass this class (vs. explicitly creating `ControllerTestDouble` implementors
themselves), and then access their chosen Controller test double through this
class’s accessors.


## `bt-host` object test doubles

By nature, any test suite using Controller test doubles uses production
instances of some layers all the way down to the Controller interface. This is
clearly sensible for unit tests of controller-interfacing code and
integration-style tests.

The Bluetooth stack is built in the form of libraries, with each library
providing a collection of public interfaces and corresponding test doubles to
enable unit testing for their clients. For unit tests of `bt-host` layers well
above the Controller, it often makes more sense to use these higher-level test
doubles rather than production instances of layers all the way down to the
Controller. These higher-level utilities are detailed below, organized by which
production protocol they imitate in tests.

### Host controller interface (HCI)

The HCI library is responsible for direct interactions with the controller in
the form of HCI messages and events.

#### [FakeConnection](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/hci/fake_connection.h)

The
[`hci::Connection` class](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/hci/connection.cc)
represents a logical link to a peer device and is used by the host stack control
plane
([gap/](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/gap),
[sm/](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/sm))
to keep track of connections. hci::Connection is specifically not a data
transport to the peer device; it strictly exposes control plane functionality
(e.g. connection state, security). While the production implementation interacts
with the Bluetooth controller using HCI commands, its test double
hci::FakeConnection allows for writing control plane unit tests without a
functional HCI backend.


#### [FakeLocalAddressDelegate](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/hci/fake_local_address_delegate.h)

The
[`hci::LocalAddressDelegate` class](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/hci/local_address_delegate.h)
provides a small interface exposing the local device's BD_ADDR address and the
Identity Resolving Key for LE Resolvable Private Addresses (RPAs), along with a
method to refresh the current RPA if needed. It is used in a variety of
situations where objects need knowledge of local address properties, including
advertising, connecting, and discovery. In production, its only implementor is
the GAP-level LocalAddressManager. Most usages of the device's local address
don't particularly depend on the rotating or private nature of local addresses,
so the FakeLocalAddressDelegate is used to have more isolated and repeatable
tests with simplified address behavior.


### Logical link control and adaption protocol (L2CAP)

L2CAP provides "higher level protocol multiplexing, packet segmentation and
reassembly, and the conveying of quality of service information." (Bluetooth
Core Specification v5.2 Vol. 3 Part A).

#### [FakeChannel](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/l2cap/fake_channel.h)([Test](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/l2cap/fake_channel_test.h))

`FakeChannel` is used as a test double for layers that directly use
[L2CAP channels](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/l2cap/channel.h).
L2CAP channels provide a logical connection to a peer service, so dependent
protocols include higher-level protocols like ATT, GATT, SMP, SDP. Based on our
[predefined terms](#Definitions), it would probably be more correct to call this
utility MockChannel, as most uses of `FakeChannel` are explicitly `Expect`ing
and `Receiv`ing specific L2CAP packets. `FakeChannelTest` is an example of the
[test harness pattern](#test-loop-harness), which provides additional
commonly-used functionality (e.g. `ReceiveAndExpect` to easily verify a specific
production response to a specific received message).


#### [FakeSignalingChannel](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/l2cap/fake_signaling_channel.h)

Used internally in L2CAP tests to mock production signaling channel behavior.
Like `FakeChannel`, it can more accurately be described as a test mock, as L2CAP
tests that use `FakeSignalingChannel` are expected to explicitly configure
packets to be expected and sent back to the production code.


### Generic attribute protocol (GATT)

GATT implements a database-like abstraction over Bluetooth connections, which
can be used to build higher-level applications and services.

#### [FakeLayer](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/gatt/fake_layer.h)([Test](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/gatt/fake_layer_test.h))

[`GATT`](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/gatt/gatt.h)
is a host-stack singleton object which owns the ATT transport channels for LE
connections and stores the current GATT clients and servers communicating with
their associated peer. It is not responsible for directly supporting application
level GATT clients or servers, but provides a basic level of GATT functionality
on top of which GATT client and server roles are built. As such, `FakeLayer`
serves as a test double for this root GATT object, allowing for unit tests of
the GATT client and server implementations to be run without dependence on
production ATT code. The GATT library also exposes a `FakeLayerTest` harness
(per above [note](#test-loop-harness)).


#### [FakeClient](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/gatt/fake_client.h)

[`gatt::Client`](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/gatt/client.h)
implements GATT client role procedures on top of a real ATT bearer object.
`gatt::Client` mainly serves as an intermediary between the lower-level,
specification-defined ATT protocol and the higher-level GATT abstractions
exposed by `bt-host` to the rest of the Fuchsia system. `FakeClient` allows
these higher-level, GATT client-dependent classes to mock out GATT client
procedures without an underlying production ATT implementation for unit tests.

### Security manager protocol (SMP)

SM "defines the protocol and behavior to manage pairing, authentication, and
encryption between LE-only or BR/EDR/LE devices." (Bluetooth Core Specification
v5.2 Vol. 3 Part H).

#### [FakeListener](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/sm/fake_phase_listener.h)

`FakeListener` is used as a test double for
[`PairingPhase`](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/sm/pairing_phase.h#25)’s
`Listener` class, which in production code is their owning class,
[`PairingState`](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/sm/pairing_state.h).
`PairingPhase`s use a pointer to `Listener` to communicate with their owning
class. `FakeListener` is another example of using the word `Fake` where `Mock`
probably makes more sense, as the main test usage is to expect that certain
calls were made to the `FakeListener` class by production `PairingPhase`
instances.


#### [TestSecurityManager(Factory)](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/sm/test_security_manager.h)

A test implementation of SM's public interface, `SecurityManager`. Provides a
`TestSecurityManagerFactory` with a `CreateSm` method which stores a reference
to the created `TestSecurityManager`. A `TestSecurityManagerFactory` can be used
to inject `TestSecurityManager`s into production code while keeping them
accessible by unit tests. The current implementation is a very minimal test spy.
It provides basic argument snooping and stub responses for a few methods and
noop implementations for others.


### Generic access protocol (GAP)

GAP integrates many of the other components of the Core Subsystem and exposes
Bluetooth functionality to application-level services and clients.

#### [FakePairingDelegate](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/gap/fake_pairing_delegate.h)

Pairing to peer devices often requires user input, but given the wide variety of
potential target devices for Fuchsia and possible input methods, there is no
consistent way for `bt-host` to directly obtain this user input.
[`gap::PairingDelegate`](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/gap/pairing_delegate.h)
provides an interface for `bt-host` to request this user i/o. This object
corresponds directly to the system-level
[`PairingDelegate` FIDL object](https://fuchsia.dev/reference/fidl/fuchsia.bluetooth.sys#PairingDelegate),
which products built on Fuchsia must implement in order to enable Bluetooth
pairing. `FakePairingDelegate` exists in order to enable unit tests of pairing
flows without real user interaction. Test code configures the test double to
expect and provide responses to pairing i/o requests from `bt-host` production
code.


#### [FakeDomain](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/data/fake_domain.h)

`data::Domain` mainly serves as an intermediary layer to aggregate different
data channel abstractions to

a.) ensure their thread safety and

b.) so higher levels can interact with a generic Bluetooth `data::Domain`
instead of lower-layer protocols.

`FakeDomain` aggregates a variety of faked out implementations of protocols
aggregated in production by `data::Domain` for unit testing of higher layers.

## `bt-host` emulation helpers

Besides mocks of single layers, `bt-host` provides the ability to emulate fake
piconets of Bluetooth peers. `bt-host` test code accesses this emulation through
the [`FakePeer`](#fakepeer-gap) and [`FakeController`](#fakecontroller) classes.
While this emulation is not quite as complex as an entire real Bluetooth stack,
there is a significant amount of behavior to emulate. As such, this behavior is
broken down into a number of "fakes" of the various Bluetooth protocols and
profiles. These test utilities are *not* injectable test doubles intended for
direct use in test case code, but abstractions within the emulation layer. As
such, reading about these test utilities will not necessarily help developers
adding feature test code. This section is for developers adding additional test
emulation features.

**Note**: For integration tests outside of `bt-host`, this emulation
      functionality can through accessed through the
      [HCI emulator](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/hci/emulator)
      utilities.

#### [FakeSignalingServer](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/fake_signaling_server.h) (L2CAP)

L2CAP communicates channel-control messages with the peer device over L2CAP
signaling channels, which exist on a per-peer-and-transport basis. In the
FakeController emulation world, FakeSignalingServer provides canned responses
for these per-logical link signaling channel messages.


#### [FakeL2cap](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/fake_l2cap.h) (L2CAP)

FakeL2cap emulates the production L2CAP layer. It provides a simplified
implementation of L2CAP packet<->channel routing, as well as methods to
dynamically configure the behavior of routed packets. The behavioral
configuration exposed by FakeL2cap is not necessarily intended for direct use in
test code. Instead, FakeL2cap usually serves as an underlying emulation layer
for other test utilities in the FakeController world. These higher-level
emulation utilities see usage in test code more than one level above L2CAP.


#### [FakeDynamicChannel](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/fake_dynamic_channel.h) (L2CAP)

`FakeDynamicChannel` provides a simplified model of real dynamic channels in the
emulator. Services which use real dynamic channels in production use
`FakeDynamicChannel` to transmit and receive data in emulated environments.


#### [FakeSdpServer](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/fake_sdp_server.h) (SDP)

`FakeSdpServer` emulates SDP Server functionality for use in integration tests
of Bluetooth flows requiring specific SDP entries. It largely leverages the
production SDP server implementation to store services and generate response
packets as necessary.


#### [FakeGattServer](https://fuchsia.googlesource.com/fuchsia/+/f28eb81421883d215a654933acf69868dfc67295/src/connectivity/bluetooth/core/bt-host/testing/fake_gatt_server.h) (GATT)
`FakeGattServer` provides a basic level of GATT server emulation. This sees use
in integration-style LE tests which use the GATT client role and require peer
GATT server functionality.
