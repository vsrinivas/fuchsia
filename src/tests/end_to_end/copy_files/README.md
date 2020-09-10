# Copy files

Verifies that a Fuchsia device can copy files to/from a host device.

On the Fuchsia device, this test exercises openssh including scp. On the host
device, this tests the `fx scp` scripts. The test may also be inadvertently
affected by any networking or storage issues on either device.

This test is important for the Workstation product, since copying files to and
from Fuchsia and host devices is something that we expect users to do. The
unique coverage this test offers is that it closely mimics the actual user
experience, including running scp on a host, so we ensure that we don't break
that flow.
