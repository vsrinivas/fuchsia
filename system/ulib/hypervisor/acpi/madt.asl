// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue

[000h 0000   4]                    Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004   4]                 Table Length : 0
[008h 0008   1]                     Revision : 4
[009h 0009   1]                     Checksum : 0
[00Ah 0010   6]                       Oem ID : "MX"
[010h 0016   8]                 Oem Table ID : "MX MADT"
[018h 0024   4]                 Oem Revision : 0
[01Ch 0028   4]              Asl Compiler ID : ""
[020h 0032   4]        Asl Compiler Revision : 0

[024h 0036   4]           Local Apic Address : FEE00000
[028h 0040   4]        Flags (decoded below) : 00000001
                         PC-AT Compatibility : 1

[02Ch 0044   1]                Subtable Type : 00 [Processor Local APIC]
[02Dh 0045   1]                       Length : 00
[02Eh 0046   1]                 Processor ID : 00
[02Fh 0047   1]                Local Apic ID : 00
[030h 0048   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1

[034h 0052   1]                Subtable Type : 01 [I/O APIC]
[035h 0053   1]                       Length : 00
[036h 0054   1]                  I/O Apic ID : 00
[037h 0055   1]                     Reserved : 00
[038h 0056   4]                      Address : FEC00000
[03ch 0060   4]                    Interrupt : 00000000
