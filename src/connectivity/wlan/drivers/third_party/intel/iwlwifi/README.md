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

- firmware (ucode): a piece of software code running inside the hardware.
  This gives the hardware some flexibility to extend features or
  fix/workaround bugs. So far we can tell from the driver source
  code, the firmware variants include:

  - MVM: a softmac implemention. This is used by the 7265D device.
  - XVT: a firmware used on virtualization environment.
  - FMAC: a fullmac implemention.
  - DVM:
  - TEST: a special firmware to debug the firmware and hardware.

- opmode: each opmode is mapped to a firmware. For example, the MVM firmware
  is an opmode. The opmode layer provides a unified interface for upper layer
  (MLME) to call so that the upper layer doesn't need to know what exact the
  firmware is.

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