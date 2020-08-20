# Fuchsia-Debian DHCP Interoperability Testing

These tests exercise Fuchsia's DHCP client abilities when interacting with a
Debian DHCP server.

The key pieces to this test are

1.  The [BUILD.gn](BUILD.gn) file in this directory which defines all of the
    test dependencies.
1.  The [component manifest](meta/dhcp_validity_test.cmx) that describes the
    structure of the test setup.
1.  The [test program](dhcp_validity/src/main.rs) that constitutes the test.
    *   Along with its [manifest](meta/dhcp_validity.cmx).

## Build File Notes

*   Any data files on which the test relies go under `resources`.
    *   In the case of the `dhcp_validity` test, there is a config file for
        dhcpd and a bash script that runs on the Debian guest. Those need to be
        included in the `resources` section so that netemul can send them to the
        guest before test program runs.
*   Component manifests for the binaries that constitute tests go under `meta`.
*   Any binary targets that the test needs go under `binaries`.
    *   The targets that generate these binaries need to be under `deps`.
*   Tests go under `tests`.
    *   Each test needs an empty binary.
    *   Each item under `tests` needs a `name` that points to an empty binary
        and has an associated component manifest in the meta directory.

## DHCP Validity Test Setup Notes

### Network Setup

```
"networks": [
    {
        "endpoints": [
            {
                "name": "client-ep"
            }
        ],
        "name": "net"
    }
]
```

This block describes all ethertap networks available to this test.

*   This test calls for a single network (named `net`).
*   One client endpoint will be created initially.

### Fuchsia Environment Setup

```
"children": [
    {
        "name": "dhcp_server",
        "services": {
            "fuchsia.net.stack.Stack": "fuchsia-pkg://fuchsia.com/netstack-debug#meta/netstack_debug.cmx",
            "fuchsia.netstack.Netstack": "fuchsia-pkg://fuchsia.com/netstack-debug#meta/netstack_debug.cmx",
            "fuchsia.posix.socket.Provider": "fuchsia-pkg://fuchsia.com/netstack-debug#meta/netstack_debug.cmx"
        },
        "setup": [
            {
                "arguments": [
                    "-e",
                    "client-ep"
                ],
                "url": "fuchsia-pkg://fuchsia.com/netemul-sandbox#meta/netstack-cfg.cmx"
            }
        ],
        "test": [
            {
                "arguments": [],
                "url": "fuchsia-pkg://fuchsia.com/netemul_dhcp_tests#meta/dhcp_validity.cmx"
            }
        ]
    }
]
```

*   `setup` contains all components and their arguments that run before the
    test. For this test `client-ep`, the endpoint connected to `net`, is added
    to the environment so that netstack can interact with the tap network.
*   `test` describes the components that constitute the test and any arguments
    that need to be provided.
*   `services` has all services on which `setup` and `test` rely.
*   `name` names the environment that netemul creates.

### Guest Creation

```
"guest": [
    {
        "files": {
            "data/dhcp_setup.sh": "/root/input/dhcp_setup.sh",
            "data/dhcpd.conf": "/etc/dhcp/dhcpd.conf"
            "data/dhcpd6.conf": "/etc/dhcp/dhcpd6.conf"
        },
        "label": "debian_guest",
        "networks": [
            "net"
        ],
        "url": "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx"
    }
],
```

*   `files` contains a source to destination mapping for input files that should
    be transferred to the guest before the test starts.
    *   These input files come from the `resources` section of the
        `test_package`.
*   `label` defines the name of the guest that will be used to look up the guest
    during the test so that the test can control it.
*   `networks` lists the ethertap networks to which the guest will be connected.
    Currently, the guest may only connect to a single network.
*   `url` defines the type of guest VM will be launched for the test. Currently,
    only Debian guests are supported.

## Test Programs Interacting With Guests

The test program configures dhcpd on the Debian guest and then waits for 30
seconds to ensure that netstack gets a DHCP address. `dhcpd.conf` and
`dhcp_setup.sh` are pushed to the guest by netemul before the test runs. The
test leverages the `GuestDiscoveryService` to get a handle to a
`GuestInteractionService` that enables the transfer of files and execution of
commands on the guest (see the [helper library](dhcp_validity/src/lib.rs)).

Using the same setup, the test program also configures dhcpd on the Debian guest
to serve DHCPv6 requests. It then launches a DHCPv6 client in Fuchsia and
expects it get a predefined list of DNS servers from dhcpd on the Debian guest.
