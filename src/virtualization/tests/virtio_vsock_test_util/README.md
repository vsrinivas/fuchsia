# Virtio Vsock Test Util

This test util is included in guest images. This (along with the other similar utilities) can be
used to provide the client side interactions for host initiated tests. The virtio vsock test util
can run in two modes, an integration test mode invoked by integration tests that runs quickly
with limited coverage, and a micro benchmark mode that stress tests the vsock device and provides
statistics.

This binary should not be invoked manually from the guest as it requires automatic interactions
from a matching host component.

## Integration Test

The integration test mode is used by the vsock integration tests:

`fx test virtualization_vsock_tests`

The host creates a Listener before running this test utility on the guest. This test utility
creates three vsock servers itself, and then connects to the host and does a write followed by
a read checking for an expected pattern.

The integration test then connects to the three vsock servers previously created by the guest,
validating:
1) Host initiated connections
2) Host terminated connections
3) Guest terminated connections

## Micro Benchmark

The micro benchmark is initiated by the guest tool. First a user must start a guest:

`$ guest launch debian`

In a second shell on the host, the user can then initiate the micro benchmark:

`$ guest vsock-perf debian`

The guest tool creates a single vsock Listener, and this test utility creates multiple connections
with known guest ports on that Listener. Most guest ports map to a socket used for a specific test
case, with the exception of the control socket which sends magic numbers from the guest to the host,
and the latency socket which runs until the host closes it.

The magic numbers are used to notify the host that a guest is ready for a test.

| Socket                           | Guest Port | Magic Number |
|----------------------------------|------------|--------------|
| Control                          | 8501       | None         |
| Latency                          | 8502       | None         |
| Single stream unidirectional     | 8503       | 123          |
| Single stream bidirectional      | 8509       | 129          |
| Multi stream unidirectional (#1) | 8504       | 124          |
| Multi stream unidirectional (#2) | 8505       | 125          |
| Multi stream unidirectional (#3) | 8506       | 126          |
| Multi stream unidirectional (#4) | 8507       | 127          |
| Multi stream unidirectional (#5) | 8508       | 128          |

### Data Corruption and Latency

This test utility continuously reads a packet and echoes it back to the host until the host closes
the connection. The host uses the data corruption check as a warmup before measuring latency.

### Single Stream Unidirectional

After sending the magic number, this test utility reads 128 MiB of data and then writes 128 MiB
of data, repeating 100 times.

### Single Stream Bidirectional

After sending the magic number this test utility spawns two threads to read and write 128 MiB as
quickly as possible to the socket. The host measures when it pushes the final byte to the socket,
and when it receives the final byte from the socket. This repeats 100 times.

### Multi Stream Unidirectional

This test utility spawns 5 threads and does 5 single stream unidirectional tests in parallel. This
test repeats 50 times as due to limited guest credit this test can take a long time.