// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mbr

import (
	"bytes"
	"encoding/binary"
	"log"
	"testing"
)

func TestWriteProtectiveMBR(t *testing.T) {
	var (
		blockSize uint64 = 512
		numBlocks uint64 = 100
	)

	// assert padding for different logical block sizes
	var w bytes.Buffer
	err := WriteProtectiveMBR(&w, blockSize, numBlocks)
	if err != nil {
		log.Fatal(err)
	}
	mbr := w.Bytes()

	// BootCode, MBR Signature, and Unknown fields all zero'd
	for i := 0; i < 446; i++ {
		if mbr[i] != 0 {
			t.Errorf("unused mbr fields should be zero'd, got %d at %d", mbr[i], i)
		}
	}

	// PartitionEntries should contain one partition covering the whole disk
	if mbr[446] != 0 {
		t.Errorf("BootIndicator should be 0, got %d", mbr[446])
	}
	startingCHS := []byte{0, 0x02, 0}
	for i, n := range mbr[447:450] {
		if startingCHS[i] != n {
			t.Errorf("StartingCHS got %d, want %d at %d", n, startingCHS[i], i)
		}
	}
	if mbr[450] != byte(GPTProtective) {
		t.Errorf("OSType got %X, want %X (GPTProtective)", mbr[450], GPTProtective)
	}

	// TODO(raggi): add tests for upper bound case
	// TODO(raggi): calculate ending CHS, unused in EFI & GPT
	//endingCHS := []byte{0x22, 0x22, 0x22}
	//for i, n := range mbr[451:454] {
	//	if endingCHS[i] != n {
	//		t.Errorf("EndingCHS got %d, want %d at %d", n, endingCHS[i], i)
	//	}
	//}

	if v := binary.LittleEndian.Uint32(mbr[454:458]); v != 1 {
		t.Errorf("StartingLBA got %d, want 1", v)
	}

	if v := binary.LittleEndian.Uint32(mbr[458:462]); uint64(v) != numBlocks-1 {
		t.Errorf("SizeInLBA got %d, want %d", v, numBlocks-1)
	}

	for i := 462; i < 510; i++ {
		if mbr[i] != 0 {
			t.Errorf("unused mbr partitions should be zero'd, got %d at %d", mbr[i], i)
		}
	}

	if mbr[510] != 0x55 || mbr[511] != 0xAA {
		t.Errorf("MBR Signature got %X, want %X", mbr[510:512], GPTSignature)
	}
}

func TestNewProtectiveMBR(t *testing.T) {
	var numBlocks uint64 = 100
	m := NewProtectiveMBR(numBlocks)

	// TODO(raggi): add coverage for fields beyond those that EFI/GPT use.

	if m.Signature != GPTSignature {
		t.Errorf("Signature got %0x, want %0x", m.Signature, GPTSignature)
	}
	partition := m.PartitionRecord[0]
	if partition.OSType != GPTProtective {
		t.Errorf("OSType got %0x, want %0x", partition.OSType, GPTProtective)
	}
	if want := [3]byte{0, 2, 0}; partition.StartingCHS != want {
		t.Errorf("StartingCHS got %0x, want %0x", partition.StartingCHS, want)
	}
	// TODO(raggi): endingCHS
	if partition.StartingLBA != 1 {
		t.Errorf("StartingLBA got %d, want %d", partition.StartingLBA, 1)
	}
	if want := uint32(numBlocks - 1); partition.SizeInLBA != want {
		t.Errorf("SizeInLBA got %d, want %d", partition.SizeInLBA, want)
	}
}

func TestReadFrom(t *testing.T) {
	var b bytes.Buffer
	if err := WriteProtectiveMBR(&b, 512, 1024); err != nil {
		t.Fatal(err)
	}

	var m MBR
	n, err := m.ReadFrom(&b)
	if err != nil {
		t.Fatal(err)
	}
	if n != MBRSize {
		t.Errorf("read length: got %d, want %d", n, MBRSize)
	}

	if m.Signature != GPTSignature {
		t.Errorf("Signature got %0x, want %0x", m.Signature, GPTSignature)
	}
	partition := m.PartitionRecord[0]
	if partition.OSType != GPTProtective {
		t.Errorf("OSType got %0x, want %0x", partition.OSType, GPTProtective)
	}
	if want := [3]byte{0, 2, 0}; partition.StartingCHS != want {
		t.Errorf("StartingCHS got %0x, want %0x", partition.StartingCHS, want)
	}
	// TODO(raggi): endingCHS
	if partition.StartingLBA != 1 {
		t.Errorf("StartingLBA got %d, want %d", partition.StartingLBA, 1)
	}
	if want := uint32(1023); partition.SizeInLBA != want {
		t.Errorf("SizeInLBA got %d, want %d", partition.SizeInLBA, want)
	}
}

func TestReadMBR(t *testing.T) {
	var b bytes.Buffer
	if err := WriteProtectiveMBR(&b, 512, 1024); err != nil {
		t.Fatal(err)
	}

	m, err := ReadMBR(&b)
	if err != nil {
		t.Fatal(err)
	}

	// TODO(raggi): add coverage for fields beyond those that EFI/GPT use.

	if m.Signature != GPTSignature {
		t.Errorf("Signature got %0x, want %0x", m.Signature, GPTSignature)
	}
	partition := m.PartitionRecord[0]
	if partition.OSType != GPTProtective {
		t.Errorf("OSType got %0x, want %0x", partition.OSType, GPTProtective)
	}
	if want := [3]byte{0, 2, 0}; partition.StartingCHS != want {
		t.Errorf("StartingCHS got %0x, want %0x", partition.StartingCHS, want)
	}
	// TODO(raggi): endingCHS
	if partition.StartingLBA != 1 {
		t.Errorf("StartingLBA got %d, want %d", partition.StartingLBA, 1)
	}
	if want := uint32(1023); partition.SizeInLBA != want {
		t.Errorf("SizeInLBA got %d, want %d", partition.SizeInLBA, want)
	}
}

func TestMBR_String(t *testing.T) {
	m := NewProtectiveMBR(100)
	want := `Disk Signature: 0x0
Signature: GPTSignature
Boot Indicator: 0x0
OS Type: GPTProtective
Starting LBA: 0x1
Size In LBA: 0x63 (99)`

	if got := m.String(); got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

// TODO(raggi): test upper bounds fields and also guarded logical block sizes
// under the required size for MBR.
