// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gpt

import (
	"bytes"
	"encoding/binary"
	"os"
	"reflect"
	"testing"
	"unicode/utf16"

	"fuchsia.googlesource.com/thinfs/mbr"
)

func TestReadHeader(t *testing.T) {
	h, err := ReadHeader(bytes.NewReader(exampleDisk()[512:]))
	if err != nil {
		t.Fatal(err)
	}

	assertStructEqual(t, h, exampleGPT().Primary.Header)
}

func TestReadPartitionEntry(t *testing.T) {
	p, err := ReadPartitionEntry(bytes.NewReader(exampleDisk()[1024:]))
	if err != nil {
		t.Fatal(err)
	}

	assertStructEqual(t, p, exampleGPT().Primary.Partitions[0])
}

func TestReadGPT(t *testing.T) {
	g, err := ReadGPT(bytes.NewReader(exampleDisk()), 512, exampleDiskSize)
	if err != nil {
		t.Fatal(err)
	}

	eg := exampleGPT()

	assertStructEqual(t, g.MBR, eg.MBR)
	assertStructEqual(t, g.Primary.Header, eg.Primary.Header)
	for i, pe := range eg.Primary.Partitions {
		assertStructEqual(t, g.Primary.Partitions[i], pe)
	}
	assertStructEqual(t, g.Backup.Header, eg.Backup.Header)
	for i, pe := range eg.Backup.Partitions {
		assertStructEqual(t, g.Backup.Partitions[i], pe)
	}
}

func TestGPTWrite(t *testing.T) {
	var b bytes.Buffer
	eg := exampleGPT()
	eg.Update(512, 4096, 0, exampleDiskSize)
	if _, err := eg.WriteTo(&b); err != nil {
		t.Fatal(err)
	}

	got := b.Bytes()
	want := exampleDisk()

	if !bytes.Equal(got, want) {
		t.Errorf("got\n%x\nwant\n%x", got, want)
	}
}

func TestUpdate(t *testing.T) {
	var (
		logicalBlockSize                 uint64 = 512
		physicalBlockSize                uint64 = 4096
		optimalTransferLengthGranularity uint64
	)
	eg := exampleGPT()

	g := GPT{
		Primary: PartitionTable{
			Header: Header{
				DiskGUID: eg.Primary.DiskGUID,
			},
			Partitions: []PartitionEntry{
				eg.Primary.Partitions[0],
			},
		},
	}

	alignment := physicalBlockSize / logicalBlockSize

	if err := g.Update(logicalBlockSize, physicalBlockSize, optimalTransferLengthGranularity, exampleDiskSize); err != nil {
		t.Fatal(err)
	}

	t.Run("Primary", func(t *testing.T) {
		assertStructEqual(t, g.Primary.Header, eg.Primary.Header)
	})
	t.Run("Backup", func(t *testing.T) {
		assertStructEqual(t, g.Backup.Header, eg.Backup.Header)
	})

	t.Run("Optimal Transfer Alignment", func(t *testing.T) {
		optimalTransferLengthGranularity = 65536
		alignment = optimalTransferLengthGranularity / logicalBlockSize

		if err := g.Update(logicalBlockSize, physicalBlockSize, optimalTransferLengthGranularity, exampleDiskSize); err != nil {
			t.Fatal(err)
		}

		if got := g.Primary.Partitions[0].StartingLBA % alignment; got != 0 {
			t.Errorf("StartingLBA alignment: got %d, want %d", got, 0)
		}
	})

	t.Run("Larger disk size", func(t *testing.T) {
		diskSize := exampleDiskSize + logicalBlockSize

		if err := g.Update(logicalBlockSize, physicalBlockSize, optimalTransferLengthGranularity, diskSize); err != nil {
			t.Fatal(err)
		}

		if got := g.Primary.AlternateLBA; got != eg.Primary.AlternateLBA+1 {
			t.Errorf("AlternateLBA: got %d, want %d", got, eg.Primary.AlternateLBA+1)
		}
	})
}

func TestValidateGPT(t *testing.T) {
	cases := []struct {
		name string
		gpt  GPT
		mut  func(g *GPT)
		err  ErrInvalidGPT
	}{
		{
			name: "valid gpt",
			mut:  func(g *GPT) {},
			err:  nil,
		},
		{
			name: "bad primary signature",
			mut:  func(g *GPT) { g.Primary.Signature = Signature{} },
			err:  ErrInvalidGPT{ErrInvalidSignature},
		},
		{
			name: "bad backup signature",
			mut:  func(g *GPT) { g.Backup.Signature = Signature{} },
			err:  ErrInvalidGPT{ErrInvalidSignature},
		},
		{
			name: "bad primary MyLBA",
			mut:  func(g *GPT) { g.Primary.MyLBA = 0 },
			err:  ErrInvalidGPT{ErrInvalidAddress},
		},
		{
			name: "bad backup AlternateLBA",
			mut:  func(g *GPT) { g.Backup.AlternateLBA = 0 },
			err:  ErrInvalidGPT{ErrInvalidAddress},
		},
		{
			name: "bad primary AlternateLBA",
			mut:  func(g *GPT) { g.Primary.AlternateLBA = 0 },
			err:  ErrInvalidGPT{ErrInvalidAddress},
		},
		{
			name: "bad backup MyLBA",
			mut:  func(g *GPT) { g.Backup.MyLBA = 0 },
			err:  ErrInvalidGPT{ErrInvalidAddress},
		},
		{
			name: "bad primary CRC",
			mut:  func(g *GPT) { g.Primary.HeaderCRC32 = 0 },
			err:  ErrInvalidGPT{ErrHeaderCRC},
		},
		{
			name: "bad backup CRC",
			mut:  func(g *GPT) { g.Backup.HeaderCRC32 = 0 },
			err:  ErrInvalidGPT{ErrHeaderCRC},
		},
		{
			name: "bad primary partition array crc",
			mut:  func(g *GPT) { g.Primary.PartitionEntryArrayCRC32 = 0 },
			err:  ErrInvalidGPT{ErrHeaderCRC},
		},
		{
			name: "bad backup partition array crc",
			mut:  func(g *GPT) { g.Backup.PartitionEntryArrayCRC32 = 0 },
			err:  ErrInvalidGPT{ErrHeaderCRC},
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			var g = exampleGPT()
			c.mut(&g)
			e := g.Validate()
			if e == nil {
				if c.err == nil {
					return
				}
				t.Errorf("got %v, want %v", e, c.err)
			}

			err := e.(ErrInvalidGPT)
			for i, want := range c.err {
				if i > len(err)-1 {
					t.Errorf("missing expected error %s", want)
					continue
				}
				if got := err[i]; got != want {
					t.Errorf("got %#v, want %#v", got, want)
				}
			}
		})
	}
}

func TestGUID(t *testing.T) {
	want := "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
	g := NewGUID(want)
	if got := g.String(); got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestGetPhysicalBlockSize(t *testing.T) {
	t.Skip("GetPhysicalBlockSize requires elevated priviliges")
	f, err := os.Open("/dev/sda")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	sz, err := GetPhysicalBlockSize(f)
	if err != nil {
		t.Fatal(err)
	}
	t.Log(sz)
}

func TestPartitionName(t *testing.T) {
	name := "abcdefghijklmnopqrstuvwxyzabcdef"
	want := [72]byte{
		0x61, 0x00, 0x62, 0x00, 0x63, 0x00, 0x64, 0x00, 0x65, 0x00, 0x66, 0x00,
		0x67, 0x00, 0x68, 0x00, 0x69, 0x00, 0x6a, 0x00, 0x6b, 0x00, 0x6c, 0x00,
		0x6d, 0x00, 0x6e, 0x00, 0x6f, 0x00, 0x70, 0x00, 0x71, 0x00, 0x72, 0x00,
		0x73, 0x00, 0x74, 0x00, 0x75, 0x00, 0x76, 0x00, 0x77, 0x00, 0x78, 0x00,
		0x79, 0x00, 0x7a, 0x00, 0x61, 0x00, 0x62, 0x00, 0x63, 0x00, 0x64, 0x00,
		0x65, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	}
	pn := NewPartitionName(name)
	if pn != want {
		t.Errorf("got %#v, want %#v", pn, want)
	}
	if got := pn.String(); got != name {
		t.Errorf("got %q, want %q", got, name)
	}
}

// returns the binary byte size of an arbitrary object. this is different from
// unsafe.Sizeof, which returns the C-ABI size of an object, which includes
// padding rules that don't apply to binary writes.
func byteSizeOf(v interface{}) int {
	var b bytes.Buffer
	binary.Write(&b, binary.LittleEndian, v)
	return b.Len()
}

func TestSizes(t *testing.T) {
	if want := byteSizeOf(PartitionEntry{}); PartitionEntrySize != want {
		t.Errorf("PartitionEntrySize: got %d, want %d", PartitionEntrySize, want)
	}
	if want := byteSizeOf(Header{}); HeaderSize != want {
		t.Errorf("HeaderSize: got %d, want %d", HeaderSize, want)
	}
}

// TODO(raggi): produce a set of examples to test instead of just one

const exampleDiskSize = 51200

func exampleDisk() []byte {
	// The contents of this method are effectively a reformatted hexdump of a real
	// image created with a disk image tool.

	var d = make([]byte, exampleDiskSize)

	copy(d[0x000001b0:], []byte{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe,
		0xff, 0xff, 0xee, 0xfe, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,
	})

	copy(d[0x000001f0:], []byte{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xaa,
		0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54, 0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00,
		0x71, 0x65, 0x6f, 0xe5, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x46, 0x91, 0x1a, 0x61, 0xba, 0xaf, 0x4c,
		0x99, 0x91, 0xb7, 0xd6, 0xe4, 0x91, 0xed, 0x50, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x5a, 0x95, 0x81, 0x63, 0x00, 0x00, 0x00, 0x00,
	})

	copy(d[0x00000400:], []byte{
		0x00, 0x53, 0x46, 0x48, 0x00, 0x00, 0xaa, 0x11, 0xaa, 0x11, 0x00, 0x30, 0x65, 0x43, 0xec, 0xac,
		0xd4, 0xf2, 0x0c, 0x08, 0x62, 0xe0, 0xcf, 0x42, 0xbf, 0x54, 0xbf, 0x34, 0x00, 0xb4, 0x89, 0x4d,
		0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x69, 0x00, 0x73, 0x00, 0x6b, 0x00,
		0x20, 0x00, 0x69, 0x00, 0x6d, 0x00, 0x61, 0x00, 0x67, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
	})

	copy(d[0x00008600:], []byte{
		0x00, 0x53, 0x46, 0x48, 0x00, 0x00, 0xaa, 0x11, 0xaa, 0x11, 0x00, 0x30, 0x65, 0x43, 0xec, 0xac,
		0xd4, 0xf2, 0x0c, 0x08, 0x62, 0xe0, 0xcf, 0x42, 0xbf, 0x54, 0xbf, 0x34, 0x00, 0xb4, 0x89, 0x4d,
		0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x69, 0x00, 0x73, 0x00, 0x6b, 0x00,
		0x20, 0x00, 0x69, 0x00, 0x6d, 0x00, 0x61, 0x00, 0x67, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
	})

	copy(d[0x0000c600:], []byte{
		0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54, 0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00,
		0x3d, 0x70, 0x6d, 0x59, 0x00, 0x00, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x46, 0x91, 0x1a, 0x61, 0xba, 0xaf, 0x4c,
		0x99, 0x91, 0xb7, 0xd6, 0xe4, 0x91, 0xed, 0x50, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x5a, 0x95, 0x81, 0x63, 0x00, 0x00, 0x00, 0x00,
	})

	return d
}

func exampleGPT() GPT {
	bname := [72]byte{}
	name16 := utf16.Encode([]rune("disk image"))
	for i, r := range name16 {
		binary.LittleEndian.PutUint16(bname[i*2:], r)
	}
	pe := PartitionEntry{
		PartitionTypeGUID:   GUIDAppleHFSPlus,
		UniquePartitionGUID: NewGUID("080CF2D4-E062-42CF-BF54-BF3400B4894D"),
		StartingLBA:         uint64(40),
		EndingLBA:           uint64(63),
		Attributes:          uint64(0),
		PartitionName:       bname,
	}
	g := GPT{
		MBR: mbr.MBR{
			Signature: mbr.GPTSignature,
			PartitionRecord: [4]mbr.PartitionRecord{
				mbr.PartitionRecord{
					StartingCHS: [3]byte{254, 255, 255},
					OSType:      mbr.GPTProtective,
					EndingCHS:   [3]byte{254, 255, 255},
					StartingLBA: 1,
					SizeInLBA:   99,
				},
			},
		},
		Primary: PartitionTable{
			Header: Header{
				Signature:                EFISignature,
				Revision:                 EFIRevision,
				HeaderSize:               HeaderSize,
				HeaderCRC32:              3849282929,
				MyLBA:                    1,
				AlternateLBA:             uint64(exampleDiskSize/512 - 1),
				FirstUsableLBA:           34,
				LastUsableLBA:            uint64((exampleDiskSize - 512 - 16384 - 512) / 512),
				DiskGUID:                 NewGUID("1A91465A-BA61-4CAF-9991-B7D6E491ED50"),
				PartitionEntryLBA:        uint64(2),
				NumberOfPartitionEntries: uint32(128),
				SizeOfPartitionEntry:     uint32(128),
				PartitionEntryArrayCRC32: uint32(1669436762),
			},
			Partitions: make([]PartitionEntry, 128),
		},
		Backup: PartitionTable{
			Header: Header{
				Signature:                EFISignature,
				Revision:                 EFIRevision,
				HeaderSize:               HeaderSize,
				HeaderCRC32:              1500344381,
				MyLBA:                    uint64(exampleDiskSize/512 - 1),
				AlternateLBA:             1,
				FirstUsableLBA:           34,
				LastUsableLBA:            uint64((exampleDiskSize - 512 - 16384 - 512) / 512),
				DiskGUID:                 NewGUID("1A91465A-BA61-4CAF-9991-B7D6E491ED50"),
				PartitionEntryLBA:        uint64(67),
				NumberOfPartitionEntries: uint32(128),
				SizeOfPartitionEntry:     uint32(128),
				PartitionEntryArrayCRC32: uint32(1669436762),
			},
			Partitions: make([]PartitionEntry, 128),
		},
	}
	g.Primary.Partitions[0] = pe
	g.Backup.Partitions[0] = pe

	return g
}

// assertStructEqual compares flat structure a to flat structure b, printing an
// error message containing the field name for any field that does not match.
func assertStructEqual(t *testing.T, got, want interface{}) {
	gotv := reflect.ValueOf(got)
	wantv := reflect.ValueOf(want)

	if gotv.Kind() != reflect.Struct || wantv.Kind() != reflect.Struct {
		t.Fatalf("not a struct: %v or %v", gotv.Kind(), wantv.Kind())
	}

	for i := 0; i < wantv.NumField(); i++ {
		if wantv.Type() != gotv.Type() {
			t.Fatalf("mismatched types: %#v and %#v", gotv, wantv)
		}

		if !wantv.Field(i).CanInterface() {
			t.Logf("cannot compare field: %v", wantv.Type().Field(i))
			continue
		}
		goti := gotv.Field(i).Interface()
		wanti := wantv.Field(i).Interface()

		if goti != wanti {
			t.Errorf("%s:\n  got %v,\n want %v", wantv.Type().Field(i).Name, goti, wanti)
		}
	}
}
