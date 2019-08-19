# Packet Capture on Fuchsia

Packet capture is a fundamental tool for developing, debugging, and testing networking.

`netdump` is a Fuchsia's packet capture tool, and is bundled into the Fuchsia image as a base package. You can run this command on the Fuchsia target, or you may run this command via `fx shell` from the development host. See the corresponding topic below.

## Quick how-to

#### First, decide which network interface you want to capture
A Fuchsia device has multiple network interfaces in general.

```shell
[target] $ net if list       # Take note of `filepath`
[target] $ net if list wlan  # Show WLAN interfaces only
[target] $ net if list eth   # Show Ethernet interfaces only
```

#### Capture all and dump to stdout for 10 sec
Specify the interface using `filepath` found in the above step. `-t {sec}` for duration. A more convenient way of specifying the network interface is planned to be done. (See crbug.com/fuchsia/4965).

```shell
[target] $ netdump -t 10 /dev/class/ethernet/000

#### Dump to a file
Use `-w {filename}`.

```shell
[target] $ netdump -t 10 -w /tmp/my_precious_packets.pcapng /dev/class/ethernet/000
```

#### Copy packet dump files from the target

```shell
[host] $ fx scp "[$(fx get-device-addr)]:/tmp/my_precious_packets.pcapng" .
```

#### Give me a hexadump
Use `--raw`

```shell
[target] $ netdump -t 10 --raw /dev/class/ethernet/000
```

#### Filter out all IPv6 packets and TCP packets whose port is either 22 or IANA-assigned http (80)
Use `-f` filter syntax.

```shell
[target] $ netdump -t 10 -f "not ( ip6 or tcp port 22,http )" /dev/class/ethernet/000
```

#### Watch ARP, DHCP, DNS packets only

```shell
[target] $ netdump -t 10 -f "arp or port dns,dhcp" /dev/class/ethernet/000
```

#### Can I run with `fx shell`?
While a better support is coming soon, in the meantime, yes - use this recipe. Note the escaping sequence magics. Also make sure to filter out "port 22" to avoid an infinite loop.

```shell
[host] $ fx shell sh -c '"netdump -t 10 -f \"not ( port 22 )\" /dev/class/ethernet/000"'
```

## Full syntax for filters
The packet filter language syntax is as follows. Keywords are in **bold**. Optional terms are in `[square brackets]`. Placeholders for literals are in `<angle brackets>`. Binary logical operators associate to the left. All keywords and port aliases should be in lower case.
<pre><code>
       expr ::= <b>(</b> expr <b>)</b>
              | <b>not</b> expr  | expr <b>and</b> expr | expr <b>or</b> expr
              | eth_expr  | host_expr     | trans_expr
length_expr ::= <b>greater</b> \<len> | <b>less</b> \<len>
       type ::= <b>src</b> | <b>dst</b>
   eth_expr ::= length_expr
              | <b>ether</b> [type] <b>host</b> \<mac_addr>
              | [<b>ether</b> <b>proto</b>] net_expr
   net_expr ::= <b>arp</b>
              | <b>vlan</b>
              | <b>ip</b>  [length_expr | host_expr | trans_expr]
              | <b>ip6</b> [length_expr | host_expr | trans_expr]
  host_expr ::= [type] <b>host</b> \<ip_addr>
 trans_expr ::= [<b>proto</b>] <b>icmp</b>
              | [<b>proto</b>] <b>tcp</b> [port_expr]
              | [<b>proto</b>] <b>udp</b> [port_expr]
              | port_expr
  port_expr ::= [type] <b>port</b> \<port_lst>
</code></pre>

*   `<len>`: Packet length in bytes. Greater or less comparison is inclusive of `len`.
*   `<mac_addr>`: MAC address, e.g. `DE:AD:BE:EF:D0:0D`. Hex digits are case-insensitive.
*   `<ip_addr>`: IP address consistent with the IP version specified previously.
    E.g. `192.168.1.10`, `2001:4860:4860::8888`.
*   `<port_lst>`: List of ports or port ranges separated by commas, e.g. `13,ssh,6000-7000,20`.
    The following aliases for defined ports and port ranges can be used as an item in the list, but
    not as part of a range (`3,dhcp,12` is allowed, `http-100` is not):

  Alias    | Port(s)
  :--------| :-------------------------
  `dhcp`   | `67-68`
  `dns`    | `53`
  `echo`   | `7`
  `ftpxfer`| `20`
  `ftpctl` | `21`
  `http`   | `80`
  `https`  | `443`
  `irc`    | `194`
  `ntp`    | `123`
  `sftp`   | `115`
  `ssh`    | `22`
  `telnet` | `23`
  `tftp`   | `69`
  `dbglog` | Netboot debug log port
  `dbgack` | Netboot debug log ack port

### Synonyms
The following aliases may be used instead of the keywords listed in the syntax:

Keyword | Alias
:-------| :----------
`ip`    | `ip4`
`port`  | `portrange`


## What fx workflow packet signatures?
There are many different kinds of services running between the Fuchsia development host and the target. Those are usually invoked by `fx` commands. Most of times, you are not interested in those packets incurred by the `fx` workflows.  The following table lists up noteworthy signatures.

|Use           |Signature                   |Reference             |
|--------------|----------------------------|----------------------|
|Logger        |port 33337                  |DEBUGLOG_PORT         |
|Logger        |port 33338                  |DEBUGLOG_ACK_PORT     |
|Bootserver    |port 33330                  |NB_SERVER_PORT        |
|Bootserver    |port 33331                  |NB_ADVERT_PORT        |
|Bootserver    |port 33332                  |NB_CMD_PORT_START     |
|Bootserver    |port 33339                  |NB_CMD_PORT_END       |
|Bootserver    |port 33340                  |NB_TFTP_OUTGOING_PORT |
|Bootserver    |port 33341                  |NB_TFTP_INCOMING_PORT |
|Package Server|port 8083                   |docs/packages.md      |
|fx shell      |port 22                     |devshell/shell        |
|target addr   |fe80::xxxx:xx4d:fexx:xxxx%XX|fx netaddr            |
|target addr   |fe80::xxxx:xxff:fexx:xxxx%XX|fx netaddr --local    |
|target addr   |fe80::xxxx:xxff:fexx:xxxx%XX|fx netaddr --fuchsia  |
|zxdb          |port 2345                   |devshell/contrib/debug|
|-             |port 65026                  |                      |
|-             |port 65268                  |                      |
|-             |1900                        |                      |


## How do I test if `netdump` is broken?
You can run some sanity checks locally.

```shell
[host] $ fx set core.x64 --with //src/connectivity:tests,//src/connectivity/network/netdump:netdump_unit_tests
# (After running your target)
[host] $ fx run-test netdump_unit_test          # unit test
[host] $ fx run-test netdump_integration_tests  # integration test
```
