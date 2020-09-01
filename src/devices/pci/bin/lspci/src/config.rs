// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::util::is_set,
    bitfield::bitfield,
    std::fmt,
    zerocopy::{AsBytes, FromBytes, LayoutVerified},
};

// PCI Local Bus Specification v3.0 section 6.1
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct Config {
    pub vendor_id: u16,
    pub device_id: u16,
    pub command: u16,
    pub status: u16,
    pub revision_id: u8,
    pub program_interface: u8,
    pub sub_class: u8,
    pub base_class: u8,
    pub cache_line_size: u8,
    pub latency_timer: u8,
    pub header_type: u8,
    pub bist: u8,
    pub base_address: [u32; 6],
    pub cardbus_cis_ptr: u32,
    pub sub_vendor_id: u16,
    pub subsystem_id: u16,
    pub expansion_rom_address: u32,
    pub capabilities_ptr: u8,
    pub reserved_0: [u8; 3],
    pub reserved_1: [u8; 4],
    pub interrupt_line: u8,
    pub interrupt_pin: u8,
    pub min_grant: u8,
    pub max_latency: u8,
}

impl Config {
    pub fn new(config: &[u8]) -> LayoutVerified<&[u8], Config> {
        let (config, _) = LayoutVerified::new_from_prefix(config).unwrap();
        config
    }
}

// PCI Local Bus Specification v3.0 6.2.2.
// Bit 7 was valid in v2.1 but in v3.0 is hardwired to 0.
bitfield! {
    pub struct CommandRegister(u16);
    pub io_space, _: 0;
    pub memory_space, _: 1;
    pub bus_master_en, _: 2;
    pub special_cycles, _: 3;
    pub mem_winv_en, _: 4;
    pub vga_snoop, _: 5;
    pub parity_error_response, _: 6;
    // 7 reserved
    pub serr_en, _: 8;
    pub fb2b_en, _: 9;
    pub int_disable, _: 10;
    // 15:11 reserved
}

impl fmt::Display for CommandRegister {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Control: I/O{} Mem{} BusMaster{} SpecCycle{} MemWINV{} VGASnoop{} ParErr{} SERR{} FastB2B{} DisINTx{}",
        is_set(self.io_space()),
        is_set(self.memory_space()),
        is_set(self.bus_master_en()),
        is_set(self.special_cycles()),
        is_set(self.mem_winv_en()),
        is_set(self.vga_snoop()),
        is_set(self.parity_error_response()),
        is_set(self.serr_en()),
        is_set(self.fb2b_en()),
        is_set(self.int_disable()))
    }
}

// PCI Local Bus Specification v3.0 6.2.3.
// Bit 6 is reserved in v3.0, but was a valid field in v2.1.
bitfield! {
    pub struct StatusRegister(u16);
    // 2:0 reserved
    pub int_status, _: 3;
    pub has_cap_list, _: 4;
    pub supports_66mhz, _: 5;
    // 6 reserved
    pub fb2b, _: 7;
    pub master_data_parity_error, _: 8;
    pub devsel_timing, _: 10, 9;
    pub signaled_target_abort, _: 11;
    pub received_target_abort, _: 12;
    pub received_master_abort, _: 13;
    pub signaled_system_error, _: 14;
    pub detected_parity_error, _: 15;
}

impl fmt::Display for StatusRegister {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Status: Cap{} 66Mhz{} FastB2B{} ParErr{} DEVSEL={} >TAbort{} <TAbort{} MAbort{} >SERR{} <PERR{} INTx{}",
        is_set(self.has_cap_list()),
        is_set(self.supports_66mhz()),
        is_set(self.fb2b()),
        is_set(self.master_data_parity_error()),
        match self.devsel_timing() {
            0 => "fast",
            1 => "medium",
            2 => "slow",
            _ => "<3?>",
        },
        is_set(self.signaled_target_abort()),
        is_set(self.received_target_abort()),
        is_set(self.received_master_abort()),
        is_set(self.signaled_system_error()),
        is_set(self.detected_parity_error()),
        is_set(self.int_status()))
    }
}
