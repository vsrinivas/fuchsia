# Netstack Inspect Data

Netstack exposes useful debugging information as inspectable data that can be
retrieved from snapshots (`inspect.json` within snapshots created by `fx
snapshot`) or by using the `iquery` tool (`fx iquery`).

In this document we list useful example queries into the inspect data that
netstack exposes.

We'll use the [`jq`] tool throughout, all queries can be used from an
`inspect.json` or `fx iquery --format=json` by piping either into `jq`, e.g.:
```
cat snapshot/inspect.json | fx jq '...'
```
```
fx iquery --format=json show netstack.cmx | fx jq '...'
```
## Inspect data

There are different sources of inspect data in Netstack, referenced by their
payload keys.

### Socket Info
`Socket Info` contains information of all currently open sockets, keyed by
socket identifier,  e.g.:
```json
{
  "12345": {
    "NetworkProtocol": "IPv6",
    "TransportProtocol": "UDP",
    "State": "BOUND",
    "LocalAddress": "[fe80::8d88:9942:4f32:7259]:546",
    "RemoteAddress": ":0",
    "BindAddress": "",
    "BindNICID": "5",
    "RegisterNICID": "5",
    "Stats": { ... }
  }
}
```

To retrieve all sockets from inspect data use:
```
fx jq '.[] | select(.moniker == "netstack.cmx") | .payload."Socket Info" | .[]?'
```

### NICs
`NICs` contains information about each of the network interfaces presently
installed in the netstack, keyed by their interface identifier, e.g:
```json
{
  "2": {
    "Name": "wlanx62",
    "NICID": "3",
    "AdminUp": "true",
    "LinkOnline": "true",
    "Up": "true",
    "Running": "true",
    "Loopback": "false",
    "Promiscuous": "false",
    "LinkAddress": "44:07:0b:e2:cf:62",
    "ProtocolAddress0": "[arp] 617270/0",
    "ProtocolAddress1": "[ipv4] 192.168.0.23/24",
    "ProtocolAddress2": "[ipv6] fe80::916b:7615:6b2e:f8b3/64",
    "ProtocolAddress3": "[ipv6] fe80::4607:bff:fee2:cf62/128",
    "DHCP enabled": "true",
    "MTU": 1500,
    "Ethernet Info": { ... },
    "DHCP Info": { ... },
    "Stats": { ... }
  }
}
```

To retrieve all NICs from inspect data use:
```
fx jq '.[] | select(.moniker == "netstack.cmx") | .payload."NICs" | .[]?'
```
To look at a single NIC with `id` or `name` simply append `| select(.NICID ==
"id")` or `| select(.Name == "name")`, respectively.

### Networking Stat Counters
`Networking Stat Counters` contain stack-global counters for traffic and errors,
e.g.:
```json
{
  "UnknownProtocolRcvdPackets": 0,
  "MalformedRcvdPackets": 0,
  "DroppedPackets": 0,
  "UDP": {
    "PacketsReceived": 51551,
    "UnknownPortErrors": 30509,
    ...
  },
  "TCP": {
    "ActiveConnectionOpenings": 4443,
    "PassiveConnectionOpenings": 53,
    ...
  },
  "IP": {...},
  "ICMP": {...}
}
```

To get counters use:
```
fx jq '.[] | select(.moniker == "netstack.cmx")
           | .payload."Networking Stat Counters"
           | select(. != null)'
```
You can append `| .TCP `, or `| .UDP` to look at only the subset of interest if
needed.

## pprof

Netstack exposes [`pprof`] data that can be used to gather more information from
the Go runtime.

A typical snapshot will contain periodic `pprof` data which is gathered at set
intervals. You can query the available `pprof` information with:
```
fx jq -c '.[] | select(.moniker == "netstack.cmx")
              | select(.payload.root.pprof != null)
              | {file: .metadata.filename, keys: (.payload.root.pprof | keys)}'
```
Which will generate output like
```
{"file":"pprof/2020-10-22T16:21:49Z.inspect","keys":["allocs","block","goroutine","heap","mutex","threadcreate"]}
{"file":"pprof/now.inspect","keys":["allocs","block","goroutine","heap","mutex","threadcreate"]}
{"file":"pprof/2020-10-22T16:19:49Z.inspect","keys":["allocs","block","goroutine","heap","mutex","threadcreate"]}
{"file":"pprof/2020-10-22T16:20:49Z.inspect","keys":["allocs","block","goroutine","heap","mutex","threadcreate"]}
```

Each of the `pprof` profiles is encoded in base64. The following line gets all
the `pprof` files and decodes them at once (remember to either pipe in your
`inspect` data or add path to your inspect file after the `jq` command):
```bash
fx jq -rc '.[]
            | select(.moniker == "netstack.cmx")
            | select(.payload.root.pprof != null)
            | . as $parent
            | (.payload.root.pprof | to_entries | .[] | ($parent | .metadata.filename) + "_" + .key + " " + .value)' | \
 while read f c; do echo $c | sed s/b64://g | base64 -d > $(echo $f | sed 's/pprof\///g'); done
```

You can modify the selector `.payload.root.pprof != null` and append `and
.metadata.filename == "pprof/now.inspect"` to only get the latest profile.

[`jq`]: https://stedolan.github.io/jq/
[`pprof`]: https://github.com/google/pprof

