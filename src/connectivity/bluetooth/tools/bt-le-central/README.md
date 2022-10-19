# bt-le-central
`bt-le-central` is a tool for acting as a device in the LE central role by using the
`fuchsia.bluetooth.le` and `fuchsia.bluetooth.gatt2` FIDL APIs to discover and
connect to peripherals and their services.

## Test

```
fx test bt-le-central-unittests
```

## Usage
Connect to peripherals with either the `scan` command or the `connect` command. Once a
peripheral has been connected, a GATT REPL will start that enables you to
interact with its services.

```
bt-le-central <command>
```

### Commands
#### `scan (--connect|--scan-count=N) [--name-filter=NAME] [--uuid-filter=UUID]`
Scan for nearby devices and optionally connect to them.
##### Options
###### `-h, --help`
###### `-s, --scan-count SCAN_COUNT`
Number of scan results to return before scanning is stopped.
###### `-c, --connect`
Connect to the first connectable scan result.
###### `-n, --name-filter NAME`
Filter by device name.
###### `-u, --uuid-filter UUID`
Filter by UUID.
#### `connect <peer-id>`
Connect to a peer using its ID.
### GATT REPL
The GATT REPL will start when a peripheral has connected. The following commands
can be entered after the `GATT>` prompt.
#### Commands
##### `help`
Print help message.
##### `list`
List discovered services.
##### `connect <index>`
Connect to a service.
##### `read-chr <id>`
Read a characteristic.
##### `read-long-chr <id> <offset> <max bytes>`
Read a long characteristic.
##### `write-chr [-w] <id> <value>`
Write to a characteristic. Use the `-w` flag to write without response.
##### `write-long-chr [-r] <id> <offset> <value>`
Write to a long characteristic. Use the `-r` flag for a reliable write.
##### `read-desc <id>`
Read a characteristic descriptor.
##### `read-long-desc <id> <offset> <max bytes>`
Read a long characteristic descriptor.
##### `write-desc <id> <value>`
Write to a characteristic descriptor.
##### `write-long-desc <id> <offset> <value>`
Write to a long characteristic descriptor.
##### `read-by-type <uuid>`
Read a characteristic or descriptor by its UUID.
###### Example
```
GATT> read-by-type 9ec813b4-256b-4090-93a8-a4f0e9107733
all values read successfully: true
[id: 3, value: [0, 0, 0, 0, 0, 0]]
```

##### `enable-notify <id>`
Enable characteristic notifications.
##### `disable-notify <id>`
Disable characteristic notifications.
##### `quit`
Quit and disconnect the peripheral.
##### `exit`
Quit and disconnect the peripheral.
