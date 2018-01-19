# Intel NUC (Kaby Lake, Skylake and Broadwell)

*** note
__WARNING:__ These are directions to configure a NUC machine to load and
boot an experimental, in-development OS.
***

## NUC Setup & Configuration

These instructions configure the NUC machine to boot from a USB flash drive.
This is a necessary step for _network boot_, where the bootloader on your USB
drive pulls your freshly-built OS across the network, from host machine to NUC.

1. Install memory (and optional SSD)
    + Remove four bottom plate screws and bottom plate
    + Install memory in the DIMM slot(s)
    + (Optional) Install SSD in M.2 slot (SATA support only; NVMe lacks a driver)
1. Boot the machine into Visual BIOS
    + Reinstall the bottom plate, attach power, and start the machine
    + Press F2 during startup to enter Visual BIOS setup
    + Mouse will be required, due to the wonders of Visual BIOS
1. Disable BIOS updates from internet (setting may not be present in newer NUCs)
    + Select the Wrench menu (upper right), then Visual Bios Settings
    + Deselect __Internet Updates__
1. Verify that your memory (and SSD) are correctly installed and detected
    + Select Advanced settings, then Main section
    + Right-side Memory Information pane should list your memory
    + Switch to Devices section
    + Select PCI tab, verify that __M.2 Slot__ is enabled
    + Select SATA tab, verify that __Chipset SATA__ is enabled
    + Both tabs (PCI and SATA) should show your SSD
1. Disable USB legacy and legacy boot
    + Still in Devices section, select USB tab
    + Deselect __USB Legacy__ support
    + In Boot section, select Priority tab
    + Deselect __Legacy Boot__ (in right-side Legacy Boot Priority pane)
    + If you see a Secure Boot tab,
    + Deselect Secure Boot in the tab (otherwise you will see an "Image
      Authorization Fail" while booting USB).
1. Configure boot ordering
    + Select Boot Configuration tab
    + Enable __Boot USB Devices First__, __Boot Network Devices Last__, and
     __Unlimited Boot to Network Attempts__
    + Network Boot (bottom left pane) should display _UEFI PXE & iSCSI_.
      *** note
      __WARNING__: DO NOT disable netbooting here or netbooting from Gigaboot and
      Zedboot may not work.
      ***
1. Disable secure boot (on machines that support it)
     + On the the Boot section, Secure Boot tab, disable __Secure Boot__
1. Save BIOS changes
     + Press F10 (or click the top right (x) button) to Save and Exit, Y to confirm
     + Device will automatically reboot and begin looking for a USB or network boot
1. Power down the NUC
1. Continue to [Bootloader setup with USB flash drive](bootloader_setup.md)

*** promo
Network booting only works with the NUC's *built-in* ethernet, netbooting via
USB-ethernet dongle is unsupported.
***

## Remote management

To enable remote management, including KVM, you also need to configure
AMT.

1. Enter Intel ME settings by pressing Ctrl+P on the boot screen
    + The first time you need to set a password, the default one is "admin"
      *** aside
      Password must be at least 8 characters long, contain both lowercase and
      uppercase characters, at least one digit and at least one non alpha-numeric
      character.
      ***
1. Configure network
    + Go to Network Setup > TCP/IP Settings > Wired LAN IPV4 Configuration
    + Disable __DHCP Mode__ and set a static __IPV4 Address__
    + Return to AMT Configuration and enable __Activate Network Access__
    + Exit Intel ME settings and save your changes

*** note
__NOTE:__ This assumes you're using NUC connected to the EdgeRouter. If
your networking setup is different, you may need a different network
configuration.
***

#### Enabling Intel AMT / vPro KVM

The Intel AMT / vPro KVM needs to be enabled before use. To do so, you
can use the `wsman` command-line utility.

The following commands assume you have set the `AMT_HOST` variable which
contains the IPv4 address you configured in the Intel ME settings,
`AMT_PASSWORD` which is the Intel ME password, and `VNC_PASSWORD` which
is going to be the VNC password.

*** aside
Password must be _exactly_ 8 characters long, contain both lowercase and
uppercase characters, at least one digit and at least one non alpha-numeric
character.
***

```
# set the VNC password
wsman put http://intel.com/wbem/wscim/1/ips-schema/1/IPS_KVMRedirectionSettingData -h ${AMT_HOST} -P 16992 -u admin -p ${AMT_PASSWORD} -k RFBPassword=${VNC_PASSWORD}
# enable KVM redirection to port 5900
wsman put http://intel.com/wbem/wscim/1/ips-schema/1/IPS_KVMRedirectionSettingData -h ${AMT_HOST} -P 16992 -u admin -p ${AMT_PASSWORD} -k Is5900PortEnabled=true
# disable opt-in policy (do not ask user for console access)
wsman put http://intel.com/wbem/wscim/1/ips-schema/1/IPS_KVMRedirectionSettingData -h ${AMT_HOST} -P 16992 -u admin -p ${AMT_PASSWORD} -k OptInPolicy=false
# disable session timeout
wsman put http://intel.com/wbem/wscim/1/ips-schema/1/IPS_KVMRedirectionSettingData -h ${AMT_HOST} -P 16992 -u admin -p ${AMT_PASSWORD} -k SessionTimeout=0
# enable KVM
wsman invoke -a RequestStateChange http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_KVMRedirectionSAP -h ${AMT_HOST} -P 16992 -u admin -p ${AMT_PASSWORD} -k RequestedState=2
```

Now, you can remotely access the NUC using any VNC client, e.g.
`vncviewer ${AMT_HOST}`.
