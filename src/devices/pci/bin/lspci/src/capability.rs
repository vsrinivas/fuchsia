// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::util::is_set,
    bitfield::bitfield,
    fidl_fuchsia_hardware_pci::Capability as FidlCapability,
    std::convert::From,
    std::fmt,
    zerocopy::{AsBytes, FromBytes, LayoutVerified},
};

// Capability types are documented in PCI Local Bus Specification v3.0 Appendix H
enum CapabilityType {
    Null,
    PciPowerManagement,
    Agp,
    VitalProductData,
    SlotIdentification,
    Msi,
    CompactPciHotSwap,
    PciX,
    HyperTransport,
    Vendor,
    DebugPort,
    CompactPciCrc,
    PciHotplug,
    PciBridgeSubsystemVendorId,
    Agp8x,
    SecureDevice,
    PciExpress,
    MsiX,
    SataDataNdxCfg,
    AdvancedFeatures,
    EnhancedAllocation,
    FlatteningPortalBridge,
    Unknown(u8),
}

impl From<u8> for CapabilityType {
    fn from(value: u8) -> Self {
        match value {
            0x00 => CapabilityType::Null,
            0x01 => CapabilityType::PciPowerManagement,
            0x02 => CapabilityType::Agp,
            0x03 => CapabilityType::VitalProductData,
            0x04 => CapabilityType::SlotIdentification,
            0x05 => CapabilityType::Msi,
            0x06 => CapabilityType::CompactPciHotSwap,
            0x07 => CapabilityType::PciX,
            0x08 => CapabilityType::HyperTransport,
            0x09 => CapabilityType::Vendor,
            0x0a => CapabilityType::DebugPort,
            0x0b => CapabilityType::CompactPciCrc,
            0x0c => CapabilityType::PciHotplug,
            0x0d => CapabilityType::PciBridgeSubsystemVendorId,
            0x0e => CapabilityType::Agp8x,
            0x0f => CapabilityType::SecureDevice,
            0x10 => CapabilityType::PciExpress,
            0x11 => CapabilityType::MsiX,
            0x12 => CapabilityType::SataDataNdxCfg,
            0x13 => CapabilityType::AdvancedFeatures,
            0x14 => CapabilityType::EnhancedAllocation,
            0x15 => CapabilityType::FlatteningPortalBridge,
            _ => CapabilityType::Unknown(value),
        }
    }
}

pub struct Capability<'a> {
    offset: usize,
    config: &'a [u8],
    cap_type: CapabilityType,
}

impl<'a> fmt::Display for Capability<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Capabilities: [{:#2x}] ", self.offset)?;
        match self.cap_type {
            CapabilityType::Null => write!(f, "Null"),
            CapabilityType::PciPowerManagement => write!(f, "PCI Power Management"),
            CapabilityType::Agp => write!(f, "AGP"),
            CapabilityType::VitalProductData => write!(f, "Vital Product Data"),
            CapabilityType::SlotIdentification => write!(f, "Slot Identification"),
            CapabilityType::Msi => self.msi(f),
            CapabilityType::CompactPciHotSwap => write!(f, "CompactPCI Hotswap"),
            CapabilityType::PciX => write!(f, "PCI-X"),
            CapabilityType::HyperTransport => write!(f, "HyperTransport"),
            CapabilityType::Vendor => self.vendor(f),
            CapabilityType::DebugPort => write!(f, "Debug Port"),
            CapabilityType::CompactPciCrc => write!(f, "CompactPCI CRC"),
            CapabilityType::PciHotplug => write!(f, "PCI Hotplug"),
            CapabilityType::PciBridgeSubsystemVendorId => write!(f, "PCI Bridge Subsystem VID"),
            CapabilityType::Agp8x => write!(f, "AGP 8x"),
            CapabilityType::SecureDevice => write!(f, "Secure Device"),
            CapabilityType::PciExpress => self.pci_express(f),
            CapabilityType::MsiX => self.msi_x(f),
            CapabilityType::SataDataNdxCfg => write!(f, "SATA Data Ndx Config"),
            CapabilityType::AdvancedFeatures => write!(f, "Advanced Features"),
            CapabilityType::EnhancedAllocation => write!(f, "Enhanced Allocations"),
            CapabilityType::FlatteningPortalBridge => write!(f, "Flattening Portal Bridge"),
            CapabilityType::Unknown(id) => write!(f, "Unknown Capability (id = {:#2x})", id),
        }
    }
}

impl<'a> Capability<'a> {
    pub fn new(capability: &'a FidlCapability, config: &'a [u8]) -> Self {
        Capability {
            offset: capability.offset as usize,
            config,
            cap_type: CapabilityType::from(capability.id),
        }
    }

    fn msi(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // MSI: Enable+ Count=1/1 Maskable- 64bit+
        // Address: 00000000fee00698  Data: 0000
        let control = MsiControl(
            ((self.config[self.offset + 3] as u16) << 8) | self.config[self.offset + 2] as u16,
        );
        write!(
            f,
            "MSI: Enable{} Count={}/{} Maskable{} 64bit{}\n",
            is_set(control.enable()),
            msi_mms_to_value(control.mms_enabled()),
            msi_mms_to_value(control.mms_capable()),
            is_set(control.pvm_capable()),
            is_set(control.can_be_64bit())
        )?;

        // Due to fxbug.dev/59245 we need to include repr(packed) in our structs.
        // Unfortunately, this results in the compiler warning of a misaligned
        // reference which requires unsafe (E0133
        // https://github.com/rust-lang/rust/issues/46043).
        unsafe {
            if control.can_be_64bit() {
                let (msi, _) = LayoutVerified::<_, Msi64Capability>::new_from_prefix(
                    &self.config[self.offset..self.config.len()],
                )
                .unwrap();
                write!(
                    f,
                    "\t\tAddress: {:#010x} {:#08x} Data: {:#06x}",
                    msi.address_upper, msi.address, msi.data
                )
            } else {
                let (msi, _) = LayoutVerified::<_, Msi32Capability>::new_from_prefix(
                    &self.config[self.offset..self.config.len()],
                )
                .unwrap();
                write!(f, "\t\tAddress: {:#010x} Data: {:#06x}", msi.address, msi.data)
            }
        }
    }

    fn msi_x(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let (msix, _) = LayoutVerified::<_, MsixCapability>::new_from_prefix(
            &self.config[self.offset..self.config.len()],
        )
        .unwrap();
        let control = MsixControl(msix.control);
        let table = MsixBarField(msix.table);
        let pba = MsixBarField(msix.pba);
        write!(
            f,
            "MSI-X: Enable{} Count={} Masked{} TBIR={} TOff={:#x} PBIR={} POff={:#x}",
            is_set(control.enable()),
            control.table_size() + 1,
            is_set(control.function_mask()),
            table.bir(),
            table.offset() << 3,
            pba.bir(),
            pba.offset() << 3,
        )
    }

    fn pci_express(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let (pcie, _) = LayoutVerified::<_, PciExpressCapability>::new_from_prefix(
            &self.config[self.offset..self.config.len()],
        )
        .unwrap();
        // PCIe Base Specification v4 section 7.5.3
        let pcie_capabilities = PcieCapabilitiesField(pcie.pcie_capabilities);
        let dev_type = PcieDevicePortType::from(pcie_capabilities.device_type());
        let slot = match dev_type {
            PcieDevicePortType::PCIEEP
            | PcieDevicePortType::LPCIEEP
            | PcieDevicePortType::RCIEP
            | PcieDevicePortType::RCEC => String::from(""),
            _ => format!(" (Slot{})", is_set(pcie_capabilities.slot_implemented())),
        };
        write!(
            f,
            "Express (v{}) {}{}, MSI {:#02x}",
            pcie_capabilities.version(),
            dev_type,
            slot,
            pcie_capabilities.irq_number()
        )
    }

    fn vendor(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Vendor Specific Information: Len={:#2x}", self.config[self.offset + 2])
    }
}

fn msi_mms_to_value(mms: u16) -> u8 {
    match mms {
        0x000 => 1,
        0x001 => 2,
        0x010 => 4,
        0x011 => 8,
        0x100 => 16,
        0x101 => 32,
        _ => 0,
    }
}

bitfield! {
    struct MsiControl(u16);
    enable, _: 0;
    mms_capable, _: 3, 1;
    mms_enabled, _: 6, 4;
    can_be_64bit, _: 7;
    pvm_capable, _: 8;
    _reserved, _: 15, 9;
}

#[derive(AsBytes, FromBytes)]
#[repr(C, packed)]
struct Msi32Capability {
    id: u8,
    next: u8,
    control: u16,
    address: u32,
    data: u16,
}

#[derive(AsBytes, FromBytes)]
#[repr(C, packed)]
struct Msi64Capability {
    id: u8,
    next: u8,
    control: u16,
    address: u32,
    address_upper: u32,
    data: u16,
}

#[derive(AsBytes, FromBytes)]
#[repr(C, packed)]
struct MsixCapability {
    id: u8,
    next: u8,
    control: u16,
    table: u32,
    pba: u32,
}

bitfield! {
    pub struct MsixControl(u16);
    table_size, _: 10, 0;
    _reserved, _: 13, 11;
    function_mask, _: 14;
    enable, _: 15;
}

bitfield! {
    pub struct MsixBarField(u32);
    bir, _: 2, 0;
    offset, _: 31, 3;
}

bitfield! {
    pub struct PcieCapabilitiesField(u16);
    version, _: 3, 0;
    device_type, _: 7, 4;
    slot_implemented, _: 8;
    irq_number, _: 13, 9;
    _reserved, _: 15, 14;
}

// PCIe Base Specification Table 7-17: PCI Express Capabilities Register
enum PcieDevicePortType {
    PCIEEP,
    LPCIEEP,
    RCIEP,
    RCEC,
    RPPCIERC,
    UPPCIES,
    DPPCIES,
    PCIE2PCIB,
    PCI2PCIEB,
    Unknown(u16),
}

impl From<u16> for PcieDevicePortType {
    fn from(value: u16) -> Self {
        match value {
            0b0000 => PcieDevicePortType::PCIEEP,
            0b0001 => PcieDevicePortType::LPCIEEP,
            0b1001 => PcieDevicePortType::RCIEP,
            0b1010 => PcieDevicePortType::RCEC,
            0b0100 => PcieDevicePortType::RPPCIERC,
            0b0101 => PcieDevicePortType::UPPCIES,
            0b0110 => PcieDevicePortType::DPPCIES,
            0b0111 => PcieDevicePortType::PCIE2PCIB,
            0b1000 => PcieDevicePortType::PCI2PCIEB,
            _ => PcieDevicePortType::Unknown(value),
        }
    }
}

impl fmt::Display for PcieDevicePortType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                PcieDevicePortType::PCIEEP => "PCI Express Endpoint",
                PcieDevicePortType::LPCIEEP => "Legacy PCI Express Endpoint",
                PcieDevicePortType::RCIEP => "Root Complex Integrated Endpoint",
                PcieDevicePortType::RCEC => "Root Complex Event Collector",
                PcieDevicePortType::RPPCIERC => "Root Port of PCI Express Root Complex",
                PcieDevicePortType::UPPCIES => "Upstream Port of PCI Express Switch",
                PcieDevicePortType::DPPCIES => "Downstream Port of PCI Express Switch",
                PcieDevicePortType::PCIE2PCIB => "PCI Express to PCI/PCI-X Bridge",
                PcieDevicePortType::PCI2PCIEB => "PCI/PCI-X to PCI Express Bridge",
                PcieDevicePortType::Unknown(x) => return write!(f, "Unknown PCIe Type ({:#x})", x),
            }
        )
    }
}

#[derive(AsBytes, FromBytes)]
#[repr(C, packed)]
struct PciExpressCapability {
    id: u8,
    next: u8,
    pcie_capabilities: u16,
    device_capabilities: u32,
    device_control: u16,
    device_status: u16,
}
