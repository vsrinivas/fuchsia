// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue

[000h 0000   4]                    Signature : "MCFG"    [Memory Mapped Configuration table]
[004h 0004   4]                 Table Length : 0
[008h 0008   1]                     Revision : 1
[009h 0009   1]                     Checksum : 0
[00Ah 0010   6]                       Oem ID : "MX"
[010h 0016   8]                 Oem Table ID : "MX MCFG"
[018h 0024   4]                 Oem Revision : 0
[01Ch 0028   4]              Asl Compiler ID : ""
[020h 0032   4]        Asl Compiler Revision : 0

[024h 0036   8]                     Reserved : 0

[02Ch 0044   8]                 Base Address : D0000000
[034h 0052   2]         Segment Group Number : 0
[036h 0054   1]             Start Bus Number : 0
[037h 0055   1]               End Bus Number : 0
[038h 0056   4]                     Reserved : 0
