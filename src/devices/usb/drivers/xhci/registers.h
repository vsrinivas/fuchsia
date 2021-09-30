// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_REGISTERS_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_REGISTERS_H_

#include <lib/ddk/hw/arch_ops.h>
#include <zircon/types.h>

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

namespace usb_xhci {

// All section references refer to xHCI specification revision 1.2
// unless stated otherwise.

constexpr static uint16_t kPrimaryInterrupter = 0;

// section 3.2.7
struct TRB {
  uint64_t ptr = 0;
  uint32_t status = 0;
  uint32_t control = 0;
};

// Section 6.4.2.2
struct CommandCompletionEvent : TRB {
  //  6.4.5
  enum CompletionCode {
    Invalid = 0,
    Success = 1,
    DataBufferError = 2,
    BabbleJS = 3,
    UsbTransactionError = 4,
    TRBError = 5,
    StallError = 6,
    ResourceError = 7,
    BandwidthError = 8,
    NoSlotsAvailableError = 9,
    InvalidStreamType = 10,
    SlotNotEnabledError = 11,
    EndpointNotEnabledError = 12,
    ShortPacket = 13,
    RingUnderrun = 14,
    RingOverrun = 15,
    VfEventRingFullError = 16,  // Only applicable to virtualized environments
    ParameterError = 17,
    BandwidthOverrunError = 18,
    ContextStateError = 19,
    NoPingResponseError = 20,
    EventRingFullError = 21,
    IncompatibleDeviceError = 22,
    MissedServiceError = 23,
    CommandRingStopped = 24,
    CommandAborted = 25,
    Stopped = 26,
    StoppedLengthInvalid = 27,
    StoppedShortPacket = 28,
    MaxExitLatencyTooLarge = 29,
    IsochBufferOverrun = 31,
    EventLostError = 32,
    UndefinedError = 33,
    InvalidStreamIdError = 34,
    SecondaryBandwidthError = 35,
    SplitTransactionError = 36,
  };
  DEF_SUBFIELD(status, 31, 24, CompletionCode);
  DEF_SUBFIELD(control, 31, 24, SlotID);
  DEF_SUBFIELD(status, 23, 0, Parameter);
  DEF_SUBFIELD(control, 15, 10, Type);
};

// Section 6.4.2.3
struct PortStatusChangeEvent : TRB {
  DEF_SUBFIELD(ptr, 31, 24, PortID);
};

// Section 6.4.2.1
struct TransferEvent : TRB {
  DEF_SUBFIELD(status, 31, 24, CompletionCode);
  DEF_SUBFIELD(control, 31, 24, SlotID);
  DEF_SUBFIELD(status, 23, 0, TransferLength);
  DEF_SUBFIELD(control, 20, 16, EndpointID);
};

// Control register portion of a TRB (section 4.11.1)
class Control : public hwreg::RegisterBase<Control, uint32_t> {
 public:
  DEF_FIELD(15, 10, Type);
  // Cycle bit
  DEF_BIT(0, Cycle);
  // EntTC -- Evaluate next TRB in chain
  // (used for scatter-gather chained transfers)
  // OR
  // Toggle Cycle for link TRBs
  DEF_BIT(1, EntTC);

  // Section 6.4.6
  enum Type {
    Normal = 1,
    Setup = 2,
    Data = 3,
    Status = 4,
    Isoch = 5,
    Link = 6,
    EventData = 7,
    Nop = 8,
    EnableSlot = 9,
    DisableSlot = 10,
    AddressDeviceCommand = 11,
    ConfigureEndpointCommand = 12,
    EvaluateContextCommand = 13,
    ResetEndpointCommand = 14,
    StopEndpointCommand = 15,
    SetTrDequeuePointerCommand = 16,
    ResetDeviceCommand = 17,
    ForceEventCommand = 18,  // Only supported in virtualized environments
    NegotiateBandwidthCommand = 19,
    SetLatencyToleranceCommand = 20,
    GetPortBandwidthCommand = 21,
    ForceHeaderCommand = 22,
    NopCommand = 23,
    GetExtendedPropertyCommand = 24,
    SetExtendedPropertyCommand = 25,
    TransferEvent = 32,
    CommandCompletionEvent = 33,
    PortStatusChangeEvent = 34,
    BandwidthRequestEvent = 35,
    DoorbellEvent = 36,  // Only supported in virtualized environments
    HostControllerEvent = 37,
    DeviceNotificationEvent = 38,
    MFIndexWrapEvent = 39,
  };

  static auto Get() { return hwreg::RegisterAddr<Control>(0x0); }

  void ToTrb(TRB* trb) {
    hwreg::RegisterMmio io(&trb->control);
    WriteTo(&io);
    hw_mb();
  }

  static auto FromTRB(TRB* trb) {
    hwreg::RegisterMmio io(&trb->control);
    return Control::Get().ReadFrom(&io);
  }
};

// Section 6.4.3.4
struct AddressDeviceStruct : TRB {
  DEF_SUBFIELD(control, 31, 24, SlotID);
  // See section 4.6.5. This should normally be set to 0.
  DEF_SUBBIT(control, 9, BSR);
  AddressDeviceStruct() {
    Control::FromTRB(this).set_Type(Control::Type::AddressDeviceCommand).ToTrb(this);
  }
};

// Section 6.4.3.3
struct DisableSlot : TRB {
  DEF_SUBFIELD(control, 31, 24, slot);
  DisableSlot() { Control::Get().FromValue(0).set_Type(Control::DisableSlot).ToTrb(this); }
};

// Section 6.4.3.7
struct ResetEndpoint : TRB {
  // Transfer State Previous
  DEF_SUBFIELD(control, 31, 24, SLOT);
  DEF_SUBFIELD(control, 20, 16, ENDPOINT);
  DEF_SUBBIT(control, 9, TSP);
  ResetEndpoint() {
    Control::Get().FromValue(0).set_Type(Control::ResetEndpointCommand).ToTrb(this);
  }
};

// Section 6.4.3.8
struct StopEndpoint : TRB {
  DEF_SUBFIELD(control, 31, 24, SLOT);
  DEF_SUBFIELD(control, 20, 16, ENDPOINT);
  StopEndpoint() { Control::Get().FromValue(0).set_Type(Control::StopEndpointCommand).ToTrb(this); }
};

// Command Ring Control Register
// section 5.4.5
class CRCR : public hwreg::RegisterBase<CRCR, uint64_t> {
 public:
  DEF_UNSHIFTED_FIELD(63, 4, PTR);
  // Command ring running
  DEF_BIT(3, CRR);
  // Command abort -- Aborts the running command and generates a
  // stopped event when complete.
  DEF_BIT(2, CA);
  // Command stop -- asynchronously aborts the running command
  // and generates a stopped event when complete.
  DEF_BIT(1, CS);
  // Consumer cycle state (see 4.9.3)
  DEF_BIT(0, RCS);
  static auto Get(uint8_t cap_length) { return hwreg::RegisterAddr<CRCR>(cap_length + 0x18); }
};

// Section 6.4.3.9
struct SetTRDequeuePointer : TRB {
  DEF_SUBBIT(ptr, 0, DCS);
  DEF_SUBFIELD(ptr, 3, 1, SCT);
  DEF_SUBFIELD(control, 31, 24, SLOT);
  DEF_SUBFIELD(control, 20, 16, ENDPOINT);
  void SetPtr(CRCR cr) {
    ptr = cr.PTR();
    set_DCS(cr.RCS());
  }
  SetTRDequeuePointer() {
    Control::Get().FromValue(0).set_Type(Control::SetTrDequeuePointerCommand).ToTrb(this);
  }
};

// Isochronous TRB (Section 6.4.1.3)
struct Isoch : TRB {
  DEF_SUBFIELD(status, 31, 22, INTERRUPTER);
  // This bit should always be set to 0. Only set to 1 for testing purposes.
  DEF_SUBBIT(control, 31, SIA);
  DEF_SUBFIELD(control, 30, 20, FrameID);
  // Number of packets remaining in this TD
  // See section 4.10.2.4
  DEF_SUBFIELD(status, 21, 17, SIZE);
  // Transfer Last Burst Packet count (number of packets in the last burst)
  // Refer to section 4.11.2.3 for more information
  DEF_SUBFIELD(control, 19, 16, TLBPC);
  DEF_SUBFIELD(status, 16, 0, LENGTH);
  // Block event interrupt -- inserts an event into the event ring
  // but does not assert the interrupt line.
  DEF_SUBBIT(control, 9, BEI);
  // Number of bursts - 1 that are required to move this TD.
  DEF_SUBFIELD(control, 8, 7, TBC);
  // Immediate data instead of ptr
  DEF_SUBBIT(control, 6, IDT);
  // Generate interrupt on completion
  DEF_SUBBIT(control, 5, IOC);
  // Set to 1 on everything except the last transfer
  DEF_SUBBIT(control, 4, CHAIN);
  // Don't snoop the bus -- go directly to memory.
  // Valid for PCIe only.
  DEF_SUBBIT(control, 3, NO_SNOOP);
  // Interrupt on Short Packet
  DEF_SUBBIT(control, 2, ISP);
  Isoch() { Control::Get().FromValue(0).set_Type(Control::Isoch).ToTrb(this); }
};

// Normal TRB (Section 6.4.1.1)
struct Normal : TRB {
  DEF_SUBFIELD(status, 31, 22, INTERRUPTER);
  // Number of packets remaining in this TD
  // See section 4.10.2.4
  DEF_SUBFIELD(status, 21, 17, SIZE);
  DEF_SUBFIELD(status, 16, 0, LENGTH);
  // Block event interrupt -- inserts an event into the event ring
  // but does not assert the interrupt line.
  DEF_SUBBIT(control, 9, BEI);
  // Immediate data instead of ptr
  DEF_SUBBIT(control, 6, IDT);
  // Generate interrupt on completion
  DEF_SUBBIT(control, 5, IOC);
  // Set to 1 on everything except the last transfer
  DEF_SUBBIT(control, 4, CHAIN);
  // Don't snoop the bus -- go directly to memory.
  // Valid for PCIe only.
  DEF_SUBBIT(control, 3, NO_SNOOP);
  // Interrupt on Short Packet
  DEF_SUBBIT(control, 2, ISP);
  Normal() { Control::Get().FromValue(0).set_Type(Control::Normal).ToTrb(this); }
};

// Setup TRB (Section 6.4.1.2.1)
struct Setup : TRB {
  enum TransferType {
    NoDataStage = 0,
    OUT = 2,
    IN = 3,
  };
  DEF_SUBFIELD(status, 31, 22, INTERRUPTER);
  DEF_SUBFIELD(status, 16, 0, length);
  // Transfer type
  DEF_SUBFIELD(control, 17, 16, TRT);
  // Immediate data instead of ptr
  DEF_SUBBIT(control, 6, IDT);
  // Interrupt on Short Packet
  // Generate interrupt on completion
  DEF_SUBBIT(control, 5, IOC);
  Setup() {
    Control::Get().FromValue(0).set_Type(Control::Setup).ToTrb(this);
    set_IDT(1);
  }
};

// Data stage TRB for control endpoint (6.4.1.2.2)
struct ControlData : TRB {
  DEF_SUBFIELD(status, 31, 22, INTERRUPTER);
  // Number of packets remaining in this TD
  // See section 4.10.2.4
  DEF_SUBFIELD(status, 21, 17, SIZE);
  DEF_SUBFIELD(status, 16, 0, LENGTH);
  // 0 == OUT, 1 == IN
  DEF_SUBBIT(control, 16, DIRECTION);
  // Immediate data instead of ptr
  DEF_SUBBIT(control, 6, IDT);
  // Generate interrupt on completion
  DEF_SUBBIT(control, 5, IOC);
  // Set to 1 on everything except the last transfer
  DEF_SUBBIT(control, 4, CHAIN);
  // Don't snoop the bus -- go directly to memory.
  // Valid for PCIe only.
  DEF_SUBBIT(control, 3, NO_SNOOP);
  // Interrupt on Short Packet
  DEF_SUBBIT(control, 2, ISP);
  ControlData() { Control::Get().FromValue(0).set_Type(Control::Data).ToTrb(this); }
};

// 6.4.1.2.3
struct Status : TRB {
  DEF_SUBFIELD(status, 31, 22, INTERRUPTER);
  // 0 == OUT, 1 == IN
  DEF_SUBBIT(control, 16, DIRECTION);
  // Generate interrupt on completion
  DEF_SUBBIT(control, 5, IOC);
  // Set to 1 on everything except the last transfer
  DEF_SUBBIT(control, 4, CHAIN);
  Status() { Control::Get().FromValue(0).set_Type(Control::Status).ToTrb(this); }
};

// Section 6.2.5.1
// TODO (bbosak): Implement USB 3.1 support
class InputContextControlField : public hwreg::RegisterBase<InputContextControlField, uint32_t> {
 public:
  DEF_FIELD(23, 16, bAlternateSetting);
  DEF_FIELD(15, 8, bInterfaceNumber);
  DEF_FIELD(7, 0, bConfigurationValue);
  static auto Get() { return hwreg::RegisterAddr<InputContextControlField>(0x0); }
};

// Register definitions -- XHCI section 5.3
class CapLength : public hwreg::RegisterBase<CapLength, uint8_t> {
 public:
  DEF_FIELD(7, 0, Length);
  static auto Get() { return hwreg::RegisterAddr<CapLength>(0x0); }
};

class HciVersion : public hwreg::RegisterBase<HciVersion, uint16_t> {
 public:
  DEF_FIELD(15, 8, Minor);
  DEF_FIELD(7, 0, Major);
  static auto Get() { return hwreg::RegisterAddr<HciVersion>(0x2); }
};

class HCSPARAMS1 : public hwreg::RegisterBase<HCSPARAMS1, uint32_t> {
 public:
  DEF_FIELD(31, 24, MaxPorts);
  DEF_FIELD(18, 8, MaxIntrs);
  DEF_FIELD(7, 0, MaxSlots);
  static auto Get() { return hwreg::RegisterAddr<HCSPARAMS1>(0x4); }
};

class HCSPARAMS2 : public hwreg::RegisterBase<HCSPARAMS2, uint32_t> {
 public:
  // Max number of ERST entries == 2^ERST_MAX
  DEF_FIELD(31, 27, MAX_SCRATCHPAD_BUFFERS_LOW);
  DEF_FIELD(25, 21, MAX_SCRATCHPAD_BUFFERS_HIGH);
  DEF_FIELD(7, 4, ERST_MAX);
  static auto Get() { return hwreg::RegisterAddr<HCSPARAMS2>(0x8); }
};

class HCCPARAMS1 : public hwreg::RegisterBase<HCCPARAMS1, uint32_t> {
 public:
  // Extended Capabilities Pointer (offset from Base MMIO address)
  DEF_FIELD(31, 16, xECP);
  DEF_BIT(2, CSZ);
  // 64-bit addressing capability
  DEF_BIT(0, AC64);
  static auto Get() { return hwreg::RegisterAddr<HCCPARAMS1>(0x10); }
};

class XECP : public hwreg::RegisterBase<XECP, uint32_t> {
 public:
  hwreg::RegisterAddr<XECP> Next() { return hwreg::RegisterAddr<XECP>(reg_addr() + (NEXT() * 4)); }
  enum Type {
    Reserved = 0,
    UsbLegacySupport = 1,
    SupportedProtocol = 2,
    ExtendedPowerManagement = 3,
    IOVirtualization = 4,
  };
  DEF_FIELD(31, 16, CAP_INFO);
  DEF_FIELD(15, 8, NEXT);
  DEF_ENUM_FIELD(Type, 7, 0, ID);
  static auto Get(HCCPARAMS1 params) { return hwreg::RegisterAddr<XECP>(params.xECP() * 4); }
};

class DoorbellOffset : public hwreg::RegisterBase<DoorbellOffset, uint32_t> {
 public:
  DEF_FIELD(31, 0, DBOFF);
  static auto Get() { return hwreg::RegisterAddr<DoorbellOffset>(0x14); }
};

// Section 5.3.8
class RuntimeRegisterOffset : public hwreg::RegisterBase<RuntimeRegisterOffset, uint32_t> {
 public:
  DEF_FIELD(31, 0, RO);
  static auto Get() { return hwreg::RegisterAddr<RuntimeRegisterOffset>(0x18); }
};

// Section 5.4.1
class USBCMD : public hwreg::RegisterBase<USBCMD, uint32_t> {
 public:
  // Enable Wrap event
  DEF_BIT(10, EWE);
  // Host system error enable
  DEF_BIT(3, HSEE);
  // Interrupt enable
  DEF_BIT(2, INTE);
  // Writing a 1 will reset the xHCI. This bit will be set to 0
  // when reset is complete. Software is responsible for re-initializing the xHCI
  // after the reset is performed.
  DEF_BIT(1, RESET);
  // Run/stop register to enable or disable the xHCI.
  // If set to 1, commands will be processed.
  // If set to 0, the xHCI will halt within 16ms.
  // Refer to USBSTS to determine the current operational status of the xHCI.
  DEF_BIT(0, ENABLE);
  static auto Get(uint8_t cap_length) { return hwreg::RegisterAddr<USBCMD>(cap_length + 0x0); }
};

class USBSTS : public hwreg::RegisterBase<USBSTS, uint32_t> {
 public:
  // Host controller (non-fatal) error.
  // When this bit is set, it indicates that an internal error within the host controller
  // has occurred. Software should respond by resetting the HCI whenever this happens.
  DEF_BIT(12, HCE);
  // Controller not ready -- software should wait until this bit is cleared
  // before performing I/O.
  DEF_BIT(11, CNR);
  // Set to 1 when an interrupt is pending.
  // This MUST be cleared before clearing any IP flags.
  DEF_BIT(3, EINT);
  // Host system (potentially fatal) error occurred. If this happens, the driver
  // should probably unbind. It is indicative of instability
  // in the connection between xHCI and the host.
  DEF_BIT(2, HSE);
  DEF_BIT(0, HCHalted);

  static auto Get(uint8_t cap_length) { return hwreg::RegisterAddr<USBSTS>(cap_length + 0x4); }
};

class USB_PAGESIZE : public hwreg::RegisterBase<USB_PAGESIZE, uint32_t> {
 public:
  DEF_FIELD(15, 0, PageSize);
  static auto Get(uint8_t cap_length) {
    return hwreg::RegisterAddr<USB_PAGESIZE>(cap_length + 0x8);
  }
};

class DCBAAP : public hwreg::RegisterBase<DCBAAP, uint64_t> {
 public:
  DEF_UNSHIFTED_FIELD(63, 6, PTR);
  static auto Get(uint8_t cap_length) { return hwreg::RegisterAddr<DCBAAP>(cap_length + 0x30); }
};

class CONFIG : public hwreg::RegisterBase<CONFIG, uint32_t> {
 public:
  DEF_FIELD(7, 0, MaxSlotsEn);
  static auto Get(uint8_t cap_length) { return hwreg::RegisterAddr<CONFIG>(cap_length + 0x38); }
};

// Section 5.4.8
class PORTSC : public hwreg::RegisterBase<PORTSC, uint32_t> {
 public:
  // Port link change
  DEF_BIT(22, PLC);
  // Port reset change
  DEF_BIT(21, PRC);
  // Overcurrent change
  DEF_BIT(20, OCC);
  // Warm port reset for USB 3.0 ports
  DEF_BIT(19, WRC);
  // Port enabled/disabled changed. Only applicable to USB 2.0 ports
  DEF_BIT(18, PEC);
  // Events -- each event must be ACKed by writing a 1 to it if set
  // Connect status change
  DEF_BIT(17, CSC);
  // Write a 1 to this field before writing to PLS
  DEF_BIT(16, LWS);
  // Port Indicator Control
  DEF_FIELD(15, 14, PIC);
  // Speed ID (see 7.2.1 to find actual speed this represents)
  DEF_FIELD(13, 10, PortSpeed);
  // Port Power bit
  DEF_BIT(9, PP);
  enum LinkState {
    U0 = 0,
    U1 = 1,
    U2 = 2,
    U3 = 3,
    Disabled = 4,
    RxDetect = 5,
    Inactive = 6,
    Polling = 7,
    Recovery = 8,
    HotReset = 9,
    ComplianceMode = 10,
    TestMode = 11,
    Resume = 15,
  };
  // Port Link State. Must write a 1 to LWS prior to writing this field.
  DEF_ENUM_FIELD(LinkState, 8, 5, PLS);
  // Port reset
  // For USB 2.0, write this bit to transition from POLLING to ENABLED state.
  // For USB 3.0, writing this bit will cause a hot reset.
  DEF_BIT(4, PR);
  // Overcurrent active. Not sure how to handle this?
  DEF_BIT(3, OCA);
  // Port enabled (write a 1 to disable it)
  // Reset the port to enable it again
  DEF_BIT(1, PED);
  // Current connect status (1 when a device is connected)
  DEF_BIT(0, CCS);
  static auto Get(uint8_t cap_length, uint16_t port) {
    return hwreg::RegisterAddr<PORTSC>(cap_length + (0x400 + (0x10 * (port - 1))));
  }
};

// Section 5.5.1
class MFINDEX : public hwreg::RegisterBase<MFINDEX, uint32_t> {
 public:
  // Interrupt pending
  DEF_FIELD(13, 0, INDEX);
  static auto Get(const RuntimeRegisterOffset& reg_offset) {
    return hwreg::RegisterAddr<MFINDEX>(reg_offset.RO());
  }
};

// Interrupter registers

// Section 5.5.2.3.1
// Event Ring Segment Table Size
class ERSTSZ : public hwreg::RegisterBase<ERSTSZ, uint32_t> {
 public:
  DEF_FIELD(15, 0, TableSize);
  static auto Get(const RuntimeRegisterOffset& reg_offset, uint32_t interrupter) {
    return hwreg::RegisterAddr<ERSTSZ>(reg_offset.RO() + 0x28 + (32 * interrupter));
  }
};

// Section 5.5.2.3.2
// Event Ring Segment Table Base Address
class ERSTBA : public hwreg::RegisterBase<ERSTBA, uint64_t> {
 public:
  // Spec incorrectly had 63, 6.
  DEF_FIELD(63, 0, Pointer);
  static auto Get(const RuntimeRegisterOffset& reg_offset, uint32_t interrupter) {
    return hwreg::RegisterAddr<ERSTBA>(reg_offset.RO() + 0x30 + (32 * interrupter));
  }
};

// Section 5.5.2.3.3
// Event Ring Dequeue Pointer
// Address overlaps EHB, which isn't supported by our register library.
// This is safe due to the page-alignment requirements of the ERDP
class ERDP : public hwreg::RegisterBase<ERDP, uint64_t> {
 public:
  // Event handler busy -- must be cleared by software
  // when the dequeue pointer register is written to
  // Refer to section 4.17.2
  DEF_UNSHIFTED_FIELD(63, 4, Pointer);
  DEF_BIT(3, EHB);
  DEF_FIELD(2, 0, DESI);
  // Event Ring Dequeue Pointer overlaps EHB.
  static auto Get(const RuntimeRegisterOffset& reg_offset, uint32_t interrupter) {
    return hwreg::RegisterAddr<ERDP>(reg_offset.RO() + 0x38 + (32 * interrupter));
  }
};

// Section 5.5.2.1
// Interrupter management
class IMAN : public hwreg::RegisterBase<IMAN, uint32_t> {
 public:
  // Interrupt enable
  DEF_BIT(1, IE);
  // Interrupt pending
  DEF_BIT(0, IP);
  // Event Ring Dequeue Pointer
  static auto Get(const RuntimeRegisterOffset& reg_offset, uint32_t interrupter) {
    return hwreg::RegisterAddr<IMAN>(reg_offset.RO() + 0x20 + (32 * interrupter));
  }
};

// Section 5.5.2.2
// Interrupter Moderation
class IMODI : public hwreg::RegisterBase<IMODI, uint32_t> {
 public:
  // Interrupt pending
  DEF_FIELD(15, 0, MODI);
  // Event Ring Dequeue Pointer
  static auto Get(const RuntimeRegisterOffset& reg_offset, uint32_t interrupter) {
    return hwreg::RegisterAddr<IMODI>(reg_offset.RO() + 0x24 + (32 * interrupter));
  }
};

// Section 5.6
class DOORBELL : public hwreg::RegisterBase<DOORBELL, uint32_t> {
 public:
  DEF_FIELD(31, 16, StreamID);
  DEF_FIELD(7, 0, Target);
  static auto Get(const DoorbellOffset& offset, uint32_t index) {
    return hwreg::RegisterAddr<DOORBELL>(offset.DBOFF() + (index * 4));
  }
};

// Section 6.2.2
struct SlotContext {
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  DEF_SUBFIELD(b, 31, 24, PORT_COUNT);
  DEF_SUBFIELD(a, 31, 27, CONTEXT_ENTRIES);
  DEF_SUBBIT(a, 26, HUB);
  DEF_SUBBIT(a, 25, MULTI_TT);
  DEF_SUBFIELD(a, 23, 20, SPEED);
  // Root Hub Port Number
  DEF_SUBFIELD(b, 23, 16, PORTNO);
  DEF_SUBFIELD(a, 19, 0, ROUTE_STRING);
  // TT Think Time
  DEF_SUBFIELD(c, 17, 16, TTT);
  DEF_SUBFIELD(b, 15, 0, MAX_EXIT_LATENCY);
  DEF_SUBFIELD(c, 15, 8, PARENT_PORT_NUMBER);
  DEF_SUBFIELD(c, 7, 0, PARENT_HUB_SLOT_ID);
  DEF_SUBFIELD(c, 31, 22, INTERRUPTER_TARGET);
};

// Section 6.2.3
struct EndpointContext {
  uint32_t a;
  uint32_t b;
  uint32_t dequeue_pointer_a;
  uint32_t dequeue_pointer_b;
  uint32_t c;
  enum EndpointType {
    Invalid = 0,
    IsochOut = 1,
    BulkOut = 2,
    InterruptOut = 3,
    Control = 4,
    IsochIn = 5,
    BulkIn = 6,
    InterruptIn = 7,
  };
  DEF_SUBFIELD(c, 31, 16, MAX_ESIT_PAYLOAD_LOW);
  // Only set if LEC = 1
  DEF_SUBFIELD(a, 31, 24, MAX_ESIT_PAYLOAD_HI);
  DEF_SUBFIELD(b, 31, 16, MAX_PACKET_SIZE);
  DEF_SUBFIELD(a, 23, 16, Interval);
  DEF_SUBFIELD(b, 15, 8, MaxBurstSize);
  DEF_SUBFIELD(c, 15, 0, AVG_TRB_LENGTH);
  DEF_SUBFIELD(a, 9, 8, Mult);

  DEF_SUBFIELD(b, 5, 3, EP_TYPE);
  // CErr shall always be set to 3.
  DEF_SUBFIELD(b, 2, 1, CErr);
  DEF_SUBBIT(dequeue_pointer_a, 0, DCS);
  void Init(EndpointType type, CRCR dequeue_pointer, uint16_t max_packet_size = 8,
            uint16_t avg_trb_length = 8) {
    uint64_t ptr = dequeue_pointer.PTR();
    dequeue_pointer_a = static_cast<uint32_t>(ptr);
    dequeue_pointer_b = static_cast<uint32_t>(ptr >> 32);
    set_EP_TYPE(type);
    set_DCS(static_cast<bool>(dequeue_pointer.RCS()));
    set_MAX_PACKET_SIZE(max_packet_size);
    set_AVG_TRB_LENGTH(avg_trb_length);
    set_CErr(3);
  }
  void Deinit() {
    a = 0;
    b = 0;
    dequeue_pointer_a = 0;
    dequeue_pointer_b = 0;
    c = 0;
  }
};

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_REGISTERS_H_
