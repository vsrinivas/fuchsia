# Intel WiFi driver (iwlwifi)

## What is This?

The modern Intel chipset integrates an on-chip WiFi feature in order to reduce
the board size and power consumption. It can be accessed via the PCIe bus.

There are some hardware variants on the different chipsets. Also there are
different firmware variants. The iwlwifi driver is the one driver to rule
them all. However, currently we only focus on the MVM firmware on the 7265D
module (the one on the Eve).

Note that on some Intel chipsets, the WiFi feature is co-existing with
Bluetooth feature. But this driver will not handle the Bluetooth. Instead,
the Bluetooth driver will handle it.

## Terminology

- iwl-drv: the Intel WiFi driver. It supports all series of Intel WiFi chipsets.
  However, an iwl-drv instance only maps to one firmware (opmode).

- firmware (ucode): a piece of software code running inside the hardware.
  This gives the hardware some flexibility to extend features or
  fix/workaround bugs. So far we can tell from the driver source
  code, the firmware variants include:

  - MVM: a softmac implemention. This is used by the 7265/8265/9260 devices.
  - XVT: a firmware used on virtualization environment.
  - FMAC: a fullmac implemention.
  - DVM:
  - TEST: a special firmware to debug the firmware and hardware.

- opmode: Each opmode is mapped to a firmware. For example, the MVM firmware
  is an opmode. The opmode layer provides a unified interface for upper layer
  (MLME) to call so that the upper layer doesn't need to know what exact the
  firmware is. Note that the opmode is determined at the initialization stage.

- MLME: a user of this softmac driver. It handles the all management packets,
  for example, the scan, associate, and authenticate protocols.

- mvm/mac80211.c: provides softmac functions like: add interface, scan, start
  AP and tx packet.

## The Driver Architecture

Below is the brief illustration of how we map the iwlwifi driver onto Fuchsia:

```
                 MLME
                  |
           --------------- wlanphy_impl_protocol_ops_t / wlan_softmac_protocol_ops_t
                  |
     ^            |
     |     +-------------+         +--------------+
     |     |  opmodes[]  | <------ | iwl_drv      |
     |     +-------------+         | calls opmode |
     |            |                | to interact  |
     |            |                | with mvm/.   |
     |            |                +--------------+
     |      +-----+-----+
     |      |     |     |    opmode provides iwl_op_mode_ops callbacks for
     |     xvt/  mvm/  FMAC  underlying layer to pass notifications from
     |      |     |     |    firmware.
iwl  |      +-----+-----+
wifi |            |
     |    ----------------- iwl_trans.h: the transportation layer provides
drv  |            |         the upper layer the unified interface to access
     |            |         the firmware/hardware.
     |            |
     |      +------------------+
     |      |                  |          ^
     |    PCIe            trans-sim.cc    | the
     |    trans                |          | simulated
     |      |             fake firmware   | environment
     |      |                  |          | for
     |      |              simulated      | testing
     |      |             environment     v
     v      |
            |
        --------- PCIe bus
            |
   real firmware/hardware
```

The highlevel initialization procedure is:

- `iwlwifi.bind` matches the PCIe device ID and this driver.

- `pcie/pcie_device.cc`: PcieDevice::DdkInit() is called due to the PCI binding (pcie/bind.cc).

- `PcieDevice::DdkInit()` then calls to `iwl_pci_probe()`, which is the Linux driver entry-point.

- In `iwl_pci_probe()`, the transporation `iwl_trans` is prepared with PCI device info. The
  `iwl_trans` also carries the chipset configuration (`struct iwl_cfg`), which is determined by
  the PCIe device ID.

- Then `iwl_drv_start()` (in `iwl-drv.c`) kicks off `iwl_load_firmware()`, which uses `iwl_cfg`
  to decide which firmware binary image file to load.

- Once the firmware is loaded, the corresponding opmode will be started off (`IWL_FW_MVM` for mvm).

- `iwl_op_mode_mvm_start()` then takes over to download the ucode to hardware, start the firmware,
  and initialize all variables.

- After the PCIe and mvm are ready, `::ddk::WlanphyImpl*()` in device.cc also implement the callback
  functions for WLAN core.  For example, the WLAN core can call `Device::WlanphyImplCreateIface()`
  to create a MAC interface for scan and association.

- Now, the driver is ready to accept requests.


## Files / Directories

### ROOT

- `iwl-*.*`: The code ported from the original driver. It could have been modified to be compiled
             on Fuchsia and to follow the Fuchsia coding style.

### fw/

The firmware runtime related functions, constants, and data structure.

Note that the firmware binary image file is parsed in `iwl_req_fw_callback()` after it is loaded
into the host memory (`struct iwl_fw fw` in `struct iwl_drv`).

The `drv->fw` then is passed into `iwl_op_mode_mvm_start()` (from `_iwl_op_mode_start()`).  The
`iwl_fw_runtime_init()` then links `mvm->fwrt` with `drv->fw` in `iwl_fw_runtime_init()`.  Now the
mvm driver has the firmware binary info and can download the ucode binary to the hardware via
`iwl_init_paging()` (see `iwl_mvm_load_rt_fw()` which is used in `iwl_mvm_up()` procedure).


- `fw/api/commands.h`: The command used by the firmware.

- `fw/file.h.*`: The data structure describing the firmware binary file format.

- `fw/img.h`: The placehold used by `iwl-drv` to hold the firmware info. Will be passed to an opmode
  (e.g. mvm).

- `fw/runtime.h`: Represents the generic firmware runtime data structure among opmodes (e.g. mvm).
  Besides the firmware binary info, it also contains the transportation info talking to the actual
  hardware (e.g. PCIe device info).

- `fw/paging.c`: The memory paging technology used by driver to download firmware ucode binary to
  the hardware. In Fuchsia, this is implemented by `io_buffer` DMA.


### pcie/

- `pcie/bind.cc`: implements the Fuchsia PCI binding mechanism.

- `pcie/pcie_device.cc`: implements wlan::iwlwifi::Device.

- `pcie/drv.c`: contains the PCIe device ID and configuration mappings.

- `pcie/trans.c`: implements `struct iwl_trans_ops`.

- `pcie/rx.c`: implements the PCIe RX ring.

- `pcie/tx.c`: implements the PCIe TX ring.


### mvm/

Contains the core functions of the driver.

- `mvm/mac80211.c`: the core file providing all MVM functions.
- `mvm/fw.c`: controls the life cycle of the firmware (e.g. loading and kick-off).
- `mvm/utils.c`: sends commands to firmware.
- `mvm/ops.c`: implements `iwl_mvm_ops`. It also maintains a table `iwl_mvm_rx_handlers[]` that
  handles all responses / notifications from the firmware (see `iwl_mvm_rx()`).
- `mvm/tx.c`: `iwl_mvm_tx_mpdu()` to transmit data plane packets out.
- `mvm/rx.c`: `iwl_mvm_rx_rx_mpdu()` to handle data plane packets.


### cfg/

Contains all the configurations of supported chipsets.

- cfg/7000.c: 7000-series chipsets (e.g. Eve).
- cfg/8000.c: 8000-series chipsets (e.g. NUC default adapter).
- cfg/9000.c: 9000-series chipsets (e.g. Atlas).
- cfg/22000.c: 22000-series chipsets. Not supported in Fuchsia yet.

### test/

Contains all testing related files.

- `test/dummy_test.cc`: example code to create a new unit test.
- `test/trans-sim.h`: the transportation that simulates the PCIe.
- `test/sim-mvm.*`: the fake code that simulates the MVM firmware.
- `test/single-ap-test.*`: a handy helper class for testing case to create a STA interface.
- `test/sim-nvm.*`: fakes a non-volatile memory of adapter (MAC address ... etc).
- `test/pcie_test.cc`: tests the code under pcie/.
- `test/wlan-device_test.cc`: tests the code in wlan-device.c.
- `test/*_test.cc`: test the MVM code.

To run the test locally, try the below commands:

  $ fx set --with-base //src/connectivity/wlan:tests
  $ fx test iwlwifi_test

## The Authenticate/Associate/Disassociate in Linux

This [link](https://android.googlesource.com/kernel/msm/+/android-msm-marlin-3.18-nougat-dr1/Documentation/networking/mac80211-auth-assoc-deauth.txt)
explains how the auth/assoc/disassoc are working in Linux.

In the Fuchsia, we simulate the behavior by going through the STA state machine and change the MAC
context in the firmware to indicate the interface is associated. See fxr/412251 for details.


## The Testing Architecture

In order to unittest the driver code, the first thing we do is fake the PCIe
transportation layer so that the driver code can talk to the simulated
transportation layer, the fake firmware and the simulated environment.

The simulated environment is a library that the WLAN driver team creates to
simulate the real-world WiFi environment. For example, a fake AP so that our
driver can associate with it. The fake firmware can co-operate with it to
return proper result back to the driver. An example is a scan command, the fake
firmware can query how many APs are around in the simulated environment and
return the scan result back to the driver.

With mocking up the hardware, a test case now can test the driver behavior with
meaningful environment setup.

## Resources

- https://wiki.gentoo.org/wiki/Iwlwifi
