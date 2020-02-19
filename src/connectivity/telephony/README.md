Telephony
=========

The Fuchsia Telephony stack currently aims to provide a fast, robust, and reliable
data connection over modern cellular protocols.

Source code:

- API
  * [Fuchsia RIL](../../../sdk/fidl/fuchsia.telephony.ril/)
  * TODO: [Modem Managment](../../../sdk/fidl/fuchsia.telephony.control/)
- Modem Services
  * [QMI RIL Service](../ril-qmi/)
- Drivers
  * [QMI USB Driver](../../drivers/telephony/qmi-usb-transport/)
- [Tools](tools/)
  * [ril-ctl](tools/ril-ctl/)


Supported Hardware:

  * Sierra Wireless EM7565

Acronyms:

  * FRIL - Fuchsia Radio Interface Layer
  * VFS - Virtual File System

## Getting Started
### Setup
Telephony is not included in the core fuchsia bringup currently. You can include the bundle like in the
example below:
```
$ fx set2 core.x64 --with //src/connectivity/telephony/
```


### API Examples
TODO(bwb): write an example app

### Tools
See the [telephony/tools](tools/) directory for the source code.

#### ril-ctl
The `ril-ctl` package is used for interacting with devices that expose the FRIL interface.
Interactive CLIs are currently incompatible with the standard `run` command, so these are launched
through /bin

connection owned by telephony service:
```
$ ril-ctl
```

exclusive connection to device:
```
$ ril-ctl -d /dev/class/qmi-transport/000
```

### Running Tests
#### Host Side Tests
A large chunk of the logic of interacting with QMI based modems is generated at compile time.
Tests for this live in `qmigen_unittests_lib_test`. You can run them on your _host_ system with:
```
$ fx run-host-tests qmigen_unittests
```

#### Unit Tets
TODO(bwb): Add name of tests when they land

### Architecture
#### QMI-based

Modem Manager watches the VFS for the qmi-usb-transport device to bind against any of the usb
devices it supports. Modem manager uses a FIDL call to acquire a transport channel to the driver. It
then starts a service that provides the FRIL, `ril-qmi`. `ril-qmi` proxies/translates the FIDL
messages spoken to the RadioInterfaceLayer interface to vendor specific calls.

`ril-ctl` tool is also able to initialize and connect `ril-qmi` to a specified device. It will also eventually
have the ability to interact with an existing `ril-qmi` through a management interface on the Modem Manager.


```

                                +----------------------+
                                |                      |
                                |     Modem Manager    +-+---+
                                |                      | |   | vfs-watcher
     +-----------------+        +----------+-----------+ |   |
     |                 |                   |             |   |
     |     ril-ctl     |                   |             |   |
     |                 |                   |             |   |
     +--------+--------+        +----------+-----------+ |   |
              |                 |                      | |   |
              +-----------------|       ril-qmi        +-+   |
                                |                      |     |
                                +----------+-----------+     |
                                           |                 |
+------------------------------------------+-----------------+--------------------+
                  Device Manager           |                 |
                                           |                 |
                               +-----------+-----------+     |
                               |                       |     |
                               |    qmi-usb-transport  +-----+
                               |                       |
                               +-----------------------+

```
