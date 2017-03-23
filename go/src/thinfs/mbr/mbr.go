// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package mbr is a minimial implementation of Master Boot Record parsing and
// writing, implemented in support of GUID Partition Table parsing and writing.
// It is based on the UEFI Specification v2.6.
package mbr

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
)

// OSType marks the partition type in an MBR Partition Record
//go:generate stringer -type=OSType
type OSType byte

// TODO(raggi): fill in other types and organize from: "Partition types" by
// Andries Brouwer: See "Links to UEFI-Related Documents" (http://uefi.org/uefi)
// under the heading "OS Type values used in the MBR disk layout".
const (
	FAT32               OSType = 0x0b
	UEFISystemPartition OSType = 0xEF
	GPTProtective       OSType = 0xEE
)

// Signature is an MBR Signature as two bytes in little endian.
//go:generate stringer -type=Signature
type Signature uint16

const (
	// GPTSignature is the partition signature of a protective MBR partition
	GPTSignature Signature = 0xAA55
)

// PartitionRecord is a type representing a legacy MBR partition record
type PartitionRecord struct {
	BootIndicator byte
	StartingCHS   [3]byte
	OSType        OSType
	EndingCHS     [3]byte
	StartingLBA   uint32
	SizeInLBA     uint32
}

// MBRSize is the size of an MBR record without logical block size padding
const MBRSize = 512

// MBR is a Go representation of a legacy Master Boot Record.
type MBR struct {
	BootCode               [424]byte // 424 bytes should be enough for anybody...
	Pad                    [16]byte  // unnamed blank space in efi spec?
	UniqueMBRDiskSignature uint32
	Unknown                [2]byte
	PartitionRecord        [4]PartitionRecord
	Signature              Signature
}

// ReadFrom reads from the given io.Reader into the receiver MBR. If an error
// occurs the returned bytes read may be incorrect.
func (m *MBR) ReadFrom(r io.Reader) (int64, error) {
	return MBRSize, binary.Read(r, binary.LittleEndian, m)
}

// WriteTo implements the io.WriterTo interface for MBR. It writes the MBR to
// w in little endian as per the GPT specification.
func (m *MBR) WriteTo(w io.Writer) (int64, error) {
	return MBRSize, binary.Write(w, binary.LittleEndian, m)
}

func (m MBR) String() string {
	var b bytes.Buffer
	fmt.Fprintf(&b, "Disk Signature: 0x%X\nSignature: %s\n",
		m.UniqueMBRDiskSignature, m.Signature.String())
	for i, p := range m.PartitionRecord {
		if p.OSType == OSType(0) && p.StartingLBA == 0 {
			continue
		} else if i > 0 {
			b.Write([]byte("\n"))
		}

		fmt.Fprintf(&b,
			"Boot Indicator: 0x%X\nOS Type: %s\nStarting LBA: 0x%X\nSize In LBA: 0x%X (%d)",
			p.BootIndicator, p.OSType.String(), p.StartingLBA, p.SizeInLBA, p.SizeInLBA)
	}
	return b.String()
}

// WriteProtectiveMBR constructs and writes a protective MBR to w. It will only
// write the first block to w (that is, blockSize bytes).
func WriteProtectiveMBR(w io.Writer, blockSize, numBlocks uint64) error {
	m := NewProtectiveMBR(numBlocks)
	_, err := m.WriteTo(w)
	if err != nil {
		return err
	}
	// TODO(raggi): consider doing this without allocating all that space
	_, err = w.Write(make([]byte, blockSize-MBRSize))
	return err
}

// NewProtectiveMBR constructs an MBR struct with fields conformant to the UEFI
// specification 2.6 "Protective MBR"
func NewProtectiveMBR(numBlocks uint64) MBR {
	var partSizeInLBA32 uint32 = 0xffffffff
	if numBlocks < uint64(partSizeInLBA32) {
		partSizeInLBA32 = uint32(numBlocks) - 1
	}

	return MBR{
		PartitionRecord: [4]PartitionRecord{
			PartitionRecord{
				OSType:      GPTProtective,
				StartingCHS: [3]byte{0, 2, 0},
				// TODO(raggi): correctly calculate endingCHS.
				StartingLBA: 1,
				SizeInLBA:   partSizeInLBA32,
			},
		},
		Signature: GPTSignature,
	}
}

// ReadMBR reads a MasterBootRecord from r.
func ReadMBR(r io.Reader) (*MBR, error) {
	m := &MBR{}
	_, err := m.ReadFrom(r)
	return m, err
}
