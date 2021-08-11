#!/usr/bin/env python3.8
#
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
decode ARM Exception Syndrome Register (ESR) values
"""

# TODO(maniscalco): Add a flag that assumes RAS is implemented and
# then further decode the RAS-specific details.

import argparse
import io
import os
import subprocess
import sys


# Return the Stage S1PTW description for the given value.
def s1ptw_desc(value):
    return {
        0b0:
            'Fault not on a stage 2 translation for a stage 1 translation table walk',
        0b1:
            'Fault on the stage 2 translation of an access for a stage 1 translation table walk',
    }.get(value, 'Unexpected S1PTW')


# Return the IFSC description for the given value.
def ifsc_desc(value):
    return {
        0b000000:
            'Address size fault, level 0 of translation or translation table base register',
        0b000001:
            'Address size fault, level 1',
        0b000010:
            'Address size fault, level 2',
        0b000011:
            'Address size fault, level 3',
        0b000100:
            'Translation fault, level 0',
        0b000101:
            'Translation fault, level 1',
        0b000110:
            'Translation fault, level 2',
        0b000111:
            'Translation fault, level 3',
        0b001001:
            'Access flag fault, level 1',
        0b001010:
            'Access flag fault, level 2',
        0b001011:
            'Access flag fault, level 3',
        0b001101:
            'Permission fault, level 1',
        0b001110:
            'Permission fault, level 2',
        0b001111:
            'Permission fault, level 3',
        0b010000:
            'Synchronous External abort, not on translation table walk',
        0b011000:
            'Synchronous parity or ECC error on memory access, not on translation table walk',
        0b010100:
            'Synchronous External abort, on translation table walk, level 0',
        0b010101:
            'Synchronous External abort, on translation table walk, level 1',
        0b010110:
            'Synchronous External abort, on translation table walk, level 2',
        0b010111:
            'Synchronous External abort, on translation table walk, level 3',
        0b011000:
            'Synchronous parity or ECC error on memory access, not on translation table walk',
        0b011100:
            'Synchronous parity or ECC error on memory access on translation table walk, level 0',
        0b011101:
            'Synchronous parity or ECC error on memory access on translation table walk, level 1',
        0b011110:
            'Synchronous parity or ECC error on memory access on translation table walk, level 2',
        0b011111:
            'Synchronous parity or ECC error on memory access on translation table walk, level 3',
        0b110000:
            'TLB conflict abort',
        0b110001:
            (
                'Unsupported atomic hardware update fault, if the implementation includes '
                'ARMv8.1-TTHM. Otherwise reserved.'),
    }.get(value, 'Unexpected IFSC')


# Return the IFSC, S1PTW, EA, FnV, and SET for the given Instruction Abort ISS.
def unpack_instruction_abort_iss(iss):
    ifsc = iss & ((1 << 6) - 1)
    iss >>= 6  # eat IFSC

    iss >>= 1  # eat RES0

    s1ptw = iss & ((1 << 1) - 1)
    iss >>= 1  # eat S1PTW

    iss >>= 1  # eat RES0

    ea = iss & ((1 << 1) - 1)
    iss >>= 1  # eat EA

    fnv = iss & ((1 << 1) - 1)
    iss >>= 1  # eat FnV

    set_value = iss & ((1 << 2) - 1)
    iss >>= 2  # eat SET
    return ifsc, s1ptw, ea, fnv, set_value


# Decode an instruction abort.
def decode_instruction_abort(ec, ec_desc, iss):
    ifsc, s1ptw, ea, fnv, set_value = unpack_instruction_abort_iss(iss)
    decode_ec(ec_desc)
    print(
        'ISS --> IFSC=0b{:06b} S1PTW=0b{:01b} EA=0b{:01b} FnV=0b{:01b} SET=0b{:02b}'
        .format(ifsc, s1ptw, ea, fnv, set_value))
    print('IFSC:', ifsc_desc(ifsc))
    print('S1PTW:', s1ptw_desc(s1ptw))
    if fnv != 0:
        print('FnV: FAR is not valid, and holds an UNKNOWN value')


# Return the DFSC description for the given value.
def dfsc_desc(value):
    return {
        0b000000:
            'Address size fault, level 0 of translation or translation table base register',
        0b000001:
            'Address size fault, level 1',
        0b000010:
            'Address size fault, level 2',
        0b000011:
            'Address size fault, level 3',
        0b000100:
            'Translation fault, level 0',
        0b000101:
            'Translation fault, level 1',
        0b000110:
            'Translation fault, level 2',
        0b000111:
            'Translation fault, level 3',
        0b001001:
            'Access flag fault, level 1',
        0b001010:
            'Access flag fault, level 2',
        0b001011:
            'Access flag fault, level 3',
        0b001101:
            'Permission fault, level 1',
        0b001110:
            'Permission fault, level 2',
        0b001111:
            'Permission fault, level 3',
        0b010000:
            'Synchronous External abort, not on translation table walk',
        0b011000:
            'Synchronous parity or ECC error on memory access, not on translation table walk',
        0b010100:
            'Synchronous External abort, on translation table walk, level 0',
        0b010101:
            'Synchronous External abort, on translation table walk, level 1',
        0b010110:
            'Synchronous External abort, on translation table walk, level 2',
        0b010111:
            'Synchronous External abort, on translation table walk, level 3',
        0b011000:
            'Synchronous parity or ECC error on memory access, not on translation table walk',
        0b011100:
            'Synchronous parity or ECC error on memory access on translation table walk, level 0',
        0b011101:
            'Synchronous parity or ECC error on memory access on translation table walk, level 1',
        0b011110:
            'Synchronous parity or ECC error on memory access on translation table walk, level 2',
        0b011111:
            'Synchronous parity or ECC error on memory access on translation table walk, level 3',
        0b100001:
            'Alignment fault',
        0b110000:
            'TLB conflict abort',
        0b110001:
            (
                'Unsupported atomic hardware update fault, if the implementation '
                'includes ARMv8.1-TTHM. Otherwise reserved.'),
        0b110100:
            'IMPLEMENTATION DEFINED fault (Lockdown)',
        0b110101:
            'IMPLEMENTATION DEFINED fault (Unsupported Exclusive or Atomic access)',
        0b111101:
            'Section Domain Fault, used only for faults reported in the PAR_EL1',
        0b111110:
            'Page Domain Fault, used only for faults reported in the PAR_EL1',
    }.get(value, 'Unexpected DFSC')


# Return the WnR description for the given value.
def wnr_desc(value):
    return {
        0b0: 'Abort caused by an instruction reading from a memory location',
        0b1: 'Abort caused by an instruction writing to a memory location',
    }.get(value, 'Unexpected WnR')


# Return the CM description for the given value.
def cm_desc(value):
    return {
        0b0:
            'The Data Abort was not generated by the execution of a cache maintenance instruction',
        0b1:
            (
                'The Data Abort was generated by the execution of a cache maintenance '
                ' instruction or by a synchronous fault on the execution of an address '
                'translation instruction (DC ZVA does not count as a cache maintenance '
                'instruction'),
    }.get(value, 'Unexpected CM')


# Return the SAS description for the given value.
def sas_desc(value):
    return {
        0b00: 'Byte',
        0b01: 'Halfword',
        0b10: 'Word',
        0b11: 'Doubleword',
    }.get(value, 'Unexpected SAS')


# Return the DFSC, WnR, S1PTW, CM, EA, FnV, SET, VNCR, AR, SF, SRT, SSE,
# SAS, and ISV for the given Data Abort ISS.
def unpack_data_abort_iss(iss):
    dfsc = iss & ((1 << 6) - 1)
    iss >>= 6  # eat DFSC

    wnr = iss & ((1 << 1) - 1)
    iss >>= 1  # eat WNR

    s1ptw = iss & ((1 << 1) - 1)
    iss >>= 1  # eat S1PTW

    cm = iss & ((1 << 1) - 1)
    iss >>= 1  # eat CM

    ea = iss & ((1 << 1) - 1)
    iss >>= 1  # eat EA

    fnv = iss & ((1 << 1) - 1)
    iss >>= 1  # eat FnV

    set_value = iss & ((1 << 2) - 1)
    iss >>= 2  # eat SET

    vncr = iss & ((1 << 1) - 1)
    iss >>= 1  # eat VNCR

    ar = iss & ((1 << 1) - 1)
    iss >>= 1  # eat AR

    sf = iss & ((1 << 1) - 1)
    iss >>= 1  # eat SF

    srt = iss & ((5 << 1) - 1)
    iss >>= 5  # eat SRT

    sse = iss & ((1 << 1) - 1)
    iss >>= 1  # eat SSE

    sas = iss & ((1 << 2) - 1)
    iss >>= 2  # eat SAS

    isv = iss & ((1 << 1) - 1)
    iss >>= 1  # eat ISV

    return dfsc, wnr, s1ptw, cm, ea, fnv, set_value, vncr, ar, sf, srt, sse, sas, isv


# Decode a data abort.
def decode_data_abort(ec, ec_desc, iss):
    dfsc, wnr, s1ptw, cm, ea, fnv, set_value, vncr, ar, sf, srt, sse, sas, isv = unpack_data_abort_iss(iss)
    decode_ec(ec_desc)
    print(
        'ISS --> DFSC=0b{:06b} WnR=0b{:01b} S1PTW=0b{:01b} CM=0b{:01b} EA=0b{:01b} FnV=0b{:01b} SET=0b{:02b} VNCR=0b{:01b} AR=0b{:01b} SF=0b{:01b} SRT=0b{:05b} SSE=0b{:01b} ISV=0b{:01b}'
        .format(
            dfsc, wnr, s1ptw, cm, ea, fnv, set_value, vncr, ar, sf, srt, sse, isv))
    print('DFSC:', dfsc_desc(dfsc))
    if ea != 1 and dfsc != 0b110101 and dfsc != 0b110001:
        print('WnR:', wnr_desc(wnr))
    print('S1PTW:', s1ptw_desc(s1ptw))
    print('CM:', cm_desc(cm))
    if dfsc == 0b100000 and fnv == 1:
        print('FnV: FAR is not valid, and holds an UNKNOWN value')
    if isv == 1:
        print('SAS: Access size is', sas_desc(sas))


# Decode just the EC.
def decode_ec(ec_desc):
    print('EC:', ec_desc)


# Decode the EC and ISS.
def decode_ec_iss(ec, iss):
    if ec == 0b000000:
        decode_ec('Unknown reason')
    elif ec == 0b000001:
        decode_ec('Trapped WFI or WFE instruction execution')
    elif ec == 0b000011:
        decode_ec(
            'Trapped MCR or MRC access with (coproc==1111) that is not reported using EC 0b000000'
        )
    elif ec == 0b000100:
        decode_ec(
            'Trapped MCRR or MRRC access with (coproc==1111) that is not reported using EC 0b000000'
        )
    elif ec == 0b000101:
        decode_ec('Trapped MCR or MRC access with (coproc==1110)')
    elif ec == 0b000110:
        decode_ec('Trapped LDC or STC access')
    elif ec == 0b000111:
        decode_ec(
            'Trapped access to SVE, Advanced SIMD, or floating-point functionality'
        )
    elif ec == 0b001000:
        decode_ec(
            'Trapped VMRS access, from ID group trap, that is not reported using EC 0b000111'
        )
    elif ec == 0b001001:
        decode_ec(
            'Trapped Pointer Authentication instruction because HCR_EL2.API==0 or SCR_EL3.API==0'
        )
    elif ec == 0b001010:
        decode_ec('Trapped execution of an LD64B, ST64B, ST64BV, or ST64BV0 instruction')
    elif ec == 0b001100:
        decode_ec('Trapped MRRC access with (coproc==1110)')
    elif ec == 0b001110:
        decode_ec('Illegal Execution state')
    elif ec == 0b010001:
        decode_ec('SVC instruction execution in AArch32 state')
    elif ec == 0b010010:
        decode_ec(
            'HVC instruction execution in AArch32 state, when HVC is not disabled'
        )
    elif ec == 0b010011:
        decode_ec(
            'SMC instruction execution in AArch32 state, when SMC is not disabled'
        )
    elif ec == 0b010101:
        decode_ec('SVC instruction execution in AArch64 state')
    elif ec == 0b010110:
        decode_ec(
            'HVC instruction execution in AArch64 state, when HVC is not disabled'
        )
    elif ec == 0b010111:
        decode_ec(
            'SMC instruction execution in AArch64 state, when SMC is not disabled'
        )
    elif ec == 0b011000:
        decode_ec(
            'Trapped MSR, MRS or System instruction execution in AArch64 state')
    elif ec == 0b011001:
        decode_ec('Trapped SVE functionality')
    elif ec == 0b011010:
        decode_ec('Trapped ERET, ERETAA, or ERETAB instruction execution')
    elif ec == 0b011100:
        decode_ec('Exception from a Pointer Authentication instruction authentication failure')
    elif ec == 0b011111:
        decode_ec('IMPLEMENTATION DEFINED exception to EL3')
    elif ec == 0b100000:
        ec_desc = 'Instruction Abort from a lower Exception level, that might be using AArch32 or AArch64'
        decode_instruction_abort(ec, ec_desc, iss)
    elif ec == 0b100001:
        ec_desc = 'Instruction Abort taken without a change in Exception level'
        decode_instruction_abort(ec, ec_desc, iss)
    elif ec == 0b100010:
        decode_ec('PC alignment fault exception')
    elif ec == 0b100100:
        ec_desc = 'Data Abort from a lower Exception level, that might be using AArch32 or AArch64'
        decode_data_abort(ec, ec_desc, iss)
    elif ec == 0b100101:
        ec_desc = 'Data Abort taken without a change in Exception level'
        decode_data_abort(ec, ec_desc, iss)
    elif ec == 0b100110:
        decode_ec('SP alignment fault exception')
    elif ec == 0b101000:
        decode_ec('Trapped floating-point exception taken from AArch32 state')
    elif ec == 0b101100:
        decode_ec('Trapped floating-point exception taken from AArch64 state')
    elif ec == 0b101111:
        decode_ec('SError interrupt')
    elif ec == 0b110000:
        decode_ec(
            'Breakpoint exception from a lower Exception level, that might be using AArch32 or AArch64'
        )
    elif ec == 0b110001:
        decode_ec(
            'Breakpoint exception taken without a change in Exception level')
    elif ec == 0b110010:
        decode_ec(
            'Software Step exception from a lower Exception level, that might be using AArch32 or AArch64'
        )
    elif ec == 0b110011:
        decode_ec(
            'Software Step exception taken without a change in Exception level')
    elif ec == 0b110100:
        decode_ec(
            'Watchpoint exception from a lower Exception level, that might be using AArch32 or AArch64'
        )
    elif ec == 0b110101:
        decode_ec(
            'Watchpoint exception taken without a change in Exception level')
    elif ec == 0b111000:
        decode_ec('BKPT instruction execution in AArch32 state')
    elif ec == 0b111010:
        decode_ec('Vector Catch exception from AArch32 state')
    elif ec == 0b111100:
        decode_ec('BRK instruction execution in AArch64 state')
    else:
        decode_ec('Unexpected EC value')


def decode_esr(esr):
    # Extract ISS from bits [24:0].
    iss_mask = (1 << 25) - 1
    iss = esr & iss_mask
    # Extract IL from bit [25].
    il = (esr >> 25) & 1
    # Extract EC from bits [31:26].
    ec = esr >> 26
    print(
        'ESR=0x{:08x} --> EC=0b{:06b} IL=0b{:01b} ISS=0x{:07x}'.format(
            esr, ec, il, iss))
    decode_ec_iss(ec, iss)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('esr', help='32-bit ESR value')
    args = parser.parse_args()

    esr = int(args.esr, 16)
    if esr > (2**32) - 1:
        print(
            'ERROR: ESR value too large to be a 32-bit value: 0x{0:02x}'.format(
                esr))
        sys.exit(1)

    decode_esr(esr)


if __name__ == '__main__':
    main()
