// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package gpt is an implementation of GUID Partition Table read and write. It
// is based on the UEFI specification v2.6.
package gpt

import (
	"bytes"
	"crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"hash/crc32"
	"io"
	"os"
	"runtime"
	"strings"
	"syscall"
	"unicode/utf16"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/mbr"
)

// ErrInvalidGPT aggregates zero or more errors that occur during validation
type ErrInvalidGPT []error

func (e ErrInvalidGPT) Error() string {
	errStrings := make([]string, len(e))
	for i, err := range e {
		errStrings[i] = err.Error()
	}
	return "gpt: invalid GPT: " + strings.Join(errStrings, " and ")
}

var (
	// ErrInvalidSignature indicates that the GPT contained an invalid signature
	ErrInvalidSignature = errors.New("gpt: invalid signature")
	// ErrInvalidAddress indicates that an LBA address in a header or partition
	// table points to an invalid location, either overlapping or out of range.
	ErrInvalidAddress = errors.New("gpt: invalid address")
	// ErrHeaderCRC indicates that a header contained an invalid CRC
	ErrHeaderCRC = errors.New("gpt: bad header CRC")

	// ErrUnsupportedPlatform is returned by functions that are not implemented on
	// the host plaform
	ErrUnsupportedPlatform = errors.New("gpt: unsupported platform")
)

// GUID is a 128 bit globally unique identifier
type GUID struct {
	TimeLow uint32
	TimeMid uint16
	TimeHi  uint16
	SeqHi   byte
	SeqLo   byte
	Node    [6]byte
}

// NewGUID constructs a GUID from a string in hexidecimal form, with arbitrary
// splits containing hyphens. It will panic for invalid hex characters.
func NewGUID(s string) GUID {
	h := strings.Join(strings.Split(s, "-"), "")

	b := make([]byte, 16)
	_, err := hex.Decode(b, []byte(h))
	if err != nil {
		panic("gpt: invalid GUID: " + err.Error())
	}

	var g GUID

	if err := binary.Read(bytes.NewReader(b), binary.BigEndian, &g); err != nil {
		panic("gpt: invalid GUID: " + err.Error())
	}

	return g
}

// NewRandomGUID generates a new entirely random GUID
func NewRandomGUID() GUID {
	// TODO(raggi): strictly speaking this isn't conformant to a paticular GUID
	// version, but it is also reasonable for the use case
	var g GUID
	err := binary.Read(rand.Reader, binary.LittleEndian, &g)
	if err != nil {
		panic(err)
	}
	return g
}

func (g GUID) String() string {
	var buf bytes.Buffer

	binary.Write(&buf, binary.BigEndian, g)

	b := buf.Bytes()
	return fmt.Sprintf("%X-%X-%X-%X-%X", b[0:4], b[4:6], b[6:8], b[8:10], b[10:16])
}

// IsZero returns true if all fields in the GUID are zero
func (g GUID) IsZero() bool {
	if g.TimeLow != 0 || g.TimeMid != 0 || g.TimeHi != 0 || g.SeqHi != 0 || g.SeqLo != 0 {
		return false
	}
	for _, b := range g.Node {
		if b != 0 {
			return false
		}
	}
	return true
}

// Signature is the representation of a GPT signature, normally `EFI PART`
type Signature [8]byte

func (s Signature) String() string {
	return string(s[:])
}

// EFISignature is the default EFI Signature for GPT `EFI PART`
var EFISignature = Signature{'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'}

// Revision is the 2 byte representation of a GPT revision number
type Revision [4]byte

func (r Revision) String() string {
	return fmt.Sprintf("%d.%d", binary.LittleEndian.Uint16(r[2:]), binary.LittleEndian.Uint16(r[0:2]))
}

// EFIRevision is the 1.0 EFI revision
var EFIRevision = Revision{0, 0, 1, 0}

// HeaderSize is the byte size of a GPT Header
const HeaderSize = 92

// Header is the Go struct represtation of a GPT header
type Header struct {
	Signature                Signature
	Revision                 Revision
	HeaderSize               uint32
	HeaderCRC32              uint32
	Reserved                 [4]byte
	MyLBA                    uint64
	AlternateLBA             uint64
	FirstUsableLBA           uint64
	LastUsableLBA            uint64
	DiskGUID                 GUID
	PartitionEntryLBA        uint64
	NumberOfPartitionEntries uint32
	SizeOfPartitionEntry     uint32
	PartitionEntryArrayCRC32 uint32
}

// ReadFrom reads from the given reader into the reciever header. If an error
// occurs, the returned bytes read may be incorrect.
func (h *Header) ReadFrom(r io.Reader) (int64, error) {
	return HeaderSize, binary.Read(r, binary.LittleEndian, h)
}

// WriteTo writes the header to the given writer. If an error ocurrs, the
// returned bytes written may be incorrect.
func (h *Header) WriteTo(w io.Writer) (int64, error) {
	return HeaderSize, binary.Write(w, binary.LittleEndian, h)
}

func (h Header) String() string {
	var b bytes.Buffer
	fmt.Fprintf(&b, "Signature: %s\n", h.Signature)
	fmt.Fprintf(&b, "Revision: %s\n", h.Revision)
	fmt.Fprintf(&b, "HeaderSize: %d\n", h.HeaderSize)
	fmt.Fprintf(&b, "HeaderCRC32: %d\n", h.HeaderCRC32)
	fmt.Fprintf(&b, "MyLBA: %d\n", h.MyLBA)
	fmt.Fprintf(&b, "AlternateLBA: %d\n", h.AlternateLBA)
	fmt.Fprintf(&b, "FirstUsableLBA: %d\n", h.FirstUsableLBA)
	fmt.Fprintf(&b, "LastUsableLBA: %d\n", h.LastUsableLBA)
	fmt.Fprintf(&b, "DiskGUID: %s\n", h.DiskGUID)
	fmt.Fprintf(&b, "PartitionEntryLBA: %d\n", h.PartitionEntryLBA)
	fmt.Fprintf(&b, "NumberOfPartitionEntries: %d\n", h.NumberOfPartitionEntries)
	fmt.Fprintf(&b, "SizeOfPartitionEntry: %d\n", h.SizeOfPartitionEntry)
	fmt.Fprintf(&b, "PartitionEntryArrayCRC32: %d\n", h.PartitionEntryArrayCRC32)
	return b.String()
}

// MinPartitionEntryArraySize is the minimum allowed size of a GPT partition array
const MinPartitionEntryArraySize = 16384

// PartitionName is a 32 character string in utf-16
type PartitionName [72]byte

// NewPartitionName constructs a partition name from the given string (encodes
// it in utf16 and truncates it to 32 characters.
func NewPartitionName(s string) PartitionName {
	var b bytes.Buffer
	var pn PartitionName
	binary.Write(&b, binary.LittleEndian, utf16.Encode([]rune(s)))
	copy(pn[:], b.Bytes())
	return pn
}

func (pn PartitionName) String() string {
	var chars []uint16
	for i := 0; i < len(pn); i += 2 {
		if pn[i] == 0 && pn[i+1] == 0 {
			break
		}
		chars = append(chars, binary.LittleEndian.Uint16(pn[i:i+2]))
	}

	return string(utf16.Decode(chars))
}

// PartitionEntrySize is the size of the partition entry structure
const PartitionEntrySize = 128

// PartitionEntry is the Go structure representation of a partition in GPT
type PartitionEntry struct {
	PartitionTypeGUID   GUID
	UniquePartitionGUID GUID
	StartingLBA         uint64
	EndingLBA           uint64
	Attributes          uint64
	PartitionName       PartitionName
}

// ReadFrom reads from the given reader into the reciever PartitionEntry. If an
// error occurs, the returned bytes read may be incorrect.
func (p *PartitionEntry) ReadFrom(r io.Reader) (int64, error) {
	return PartitionEntrySize, binary.Read(r, binary.LittleEndian, p)
}

// WriteTo writes the PartitionEntry to the given writer.
func (p *PartitionEntry) WriteTo(w io.Writer) (int64, error) {
	return PartitionEntrySize, binary.Write(w, binary.LittleEndian, p)
}

// IsZero returns true if the partition entry is unused
func (p *PartitionEntry) IsZero() bool {
	if p.UniquePartitionGUID == GUIDUnused {
		return true
	}
	return false
}

func (p PartitionEntry) String() string {
	var b bytes.Buffer

	fmt.Fprintf(&b, "PartitionTypeGUID: %s\n", p.PartitionTypeGUID)
	fmt.Fprintf(&b, "UniquePartitionGUID: %s\n", p.UniquePartitionGUID)
	fmt.Fprintf(&b, "StartingLBA: %d\n", p.StartingLBA)
	fmt.Fprintf(&b, "EndingLBA: %d\n", p.EndingLBA)
	fmt.Fprintf(&b, "Attributes: %d\n", p.Attributes)
	fmt.Fprintf(&b, "PartitionName: %s\n", p.PartitionName)

	return b.String()
}

// Partition Attributes
const (
	RequiredPartition  = 0
	NoBlockIOProtocol  = 1
	LegacyBIOSBootable = 2

	MicrosoftReadOnly      = 60
	MicrosoftShadowCopy    = 61
	MicrosoftHidden        = 62
	MicrosoftNoDriveLetter = 63
)

// PartitionGUIDs:
var (
	GUIDUnused           = NewGUID("00000000-0000-0000-0000-000000000000")
	GUIDMBR              = NewGUID("024DEE41-33E7-11D3-9D69-0008C781F39F")
	GUIDEFI              = NewGUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
	GUIDBIOS             = NewGUID("21686148-6449-6E6F-744E-656564454649")
	GUIDIntelFastFlash   = NewGUID("D3BFE2DE-3DAF-11DF-BA40-E3A556D89593")
	GUIDSonyBoot         = NewGUID("F4019732-066E-4E12-8273-346C5641494F")
	GUIDLenovoBoot       = NewGUID("BFBFAFE7-A34F-448A-9A5B-6213EB736C22")
	GUIDAppleHFSPlus     = NewGUID("48465300-0000-11AA-AA11-00306543ECAC")
	GUIDAppleUFS         = NewGUID("55465300-0000-11AA-AA11-00306543ECAC")
	GUIDAppleBoot        = NewGUID("426F6F74-0000-11AA-AA11-00306543ECAC")
	GUIDAppleRaid        = NewGUID("52414944-0000-11AA-AA11-00306543ECAC")
	GUIDAppleOfflineRAID = NewGUID("52414944-5F4F-11AA-AA11-00306543ECAC")
	GUIDAppleLabel       = NewGUID("4C616265-6C00-11AA-AA11-00306543ECAC")

	GUIDFuchsiaSystem = NewGUID("606B000B-B7C7-4653-A7D5-B737332C899D")
	GUIDFuchsiaData   = NewGUID("08185F0C-892D-428A-A789-DBEEC8F55E6A")
	GUIDFuchsiaBlob   = NewGUID("2967380E-134C-4CBB-B6DA-17E7CE1CA45D")
)

// ReadHeader reads a single GPT header from r.
func ReadHeader(r io.Reader) (Header, error) {
	var h Header
	_, err := h.ReadFrom(r)
	return h, err
}

// ReadPartitionEntry reads a single GPT PartitionEntry from r.
func ReadPartitionEntry(r io.Reader) (PartitionEntry, error) {
	var p PartitionEntry
	return p, binary.Read(r, binary.LittleEndian, &p)
}

// PartitionArray is an array of PartitionEntry
type PartitionArray []PartitionEntry

// WriteTo implements io.WriterTo for PartitionArray. Note that it writes only
// the partition entries stored in the receiver array. Callers are responsible
// for any necessary zero-ing of non-present zero partition entries.
func (pa PartitionArray) WriteTo(w io.Writer) (int64, error) {
	var total int64
	for _, pe := range pa {
		n, err := pe.WriteTo(w)
		total += n
		if err != nil {
			return total, err
		}
	}
	return total, nil
}

// PartitionTable is a header followed by an array of partiton entries
type PartitionTable struct {
	Header
	Partitions PartitionArray
}

// ComputePartitionArrayCRC32 calculates the CRC32 of the contained partition
// array. It does not write the result back to the partition table.
func (pt PartitionTable) ComputePartitionArrayCRC32() uint32 {
	partitionTableCRC32 := crc32.NewIEEE()

	for _, p := range pt.Partitions {
		if _, err := p.WriteTo(partitionTableCRC32); err != nil {
			panic(err)
		}
	}
	// write any left over empty partition entries
	var pe PartitionEntry
	for i := uint32(len(pt.Partitions)); i < pt.NumberOfPartitionEntries; i++ {
		if _, err := pe.WriteTo(partitionTableCRC32); err != nil {
			panic(err)
		}
	}

	return partitionTableCRC32.Sum32()
}

// ComputeHeaderCRC32 calculates the CRC32 of the header. Users may need to call
// ComputePartitionArrayCRC32 before calling this method. It does not write the
// result back to the partition table.
func (pt PartitionTable) ComputeHeaderCRC32() uint32 {
	headerCRC32 := crc32.NewIEEE()
	pt.HeaderCRC32 = 0
	if _, err := pt.WriteTo(headerCRC32); err != nil {
		panic(err)
	}

	return headerCRC32.Sum32()
}

func (pt PartitionTable) String() string {
	var b bytes.Buffer

	fmt.Fprintf(&b, "%s\n", pt.Header)
	for i, p := range pt.Partitions {
		if p.IsZero() {
			continue
		}
		fmt.Fprintf(&b, "Partition %d:\n%s\n", i, p)
	}

	return b.String()
}

// PartitionArrayPad calculates the amount of space that must be zero-written
// between the last partition contained in the PartitionArray and the end of the
// on-disk partition array size.
func (pt PartitionTable) PartitionArrayPad() int64 {
	return int64(pt.NumberOfPartitionEntries*pt.SizeOfPartitionEntry) - int64(len(pt.Partitions)*int(pt.SizeOfPartitionEntry))
}

// GPT is a wrapper around a disks MBR, Primary and Backup PartitionTable and
// block size metadata.
type GPT struct {
	MBR     mbr.MBR
	Primary PartitionTable
	Backup  PartitionTable

	logical, physical, optimal, size uint64 // block, transfer and sizes in bytes
}

// ReadGPT reads a GPT from r using the given logical block size.
func ReadGPT(r io.ReadSeeker, blockSize uint64, diskSize uint64) (GPT, error) {
	var g GPT

	if _, err := g.MBR.ReadFrom(r); err != nil {
		return g, err
	}

	if _, err := r.Seek(int64(blockSize), io.SeekStart); err != nil {
		return g, err
	}

	if _, err := g.Primary.ReadFrom(r); err != nil {
		return g, err
	}

	if g.Primary.ComputeHeaderCRC32() == g.Primary.HeaderCRC32 {
		ptLoc := int64(g.Primary.PartitionEntryLBA * uint64(blockSize))
		if _, err := r.Seek(ptLoc, io.SeekStart); err != nil {
			return g, err
		}

		// Avoid divide by zero, recover any possibly readable data..
		if g.Primary.SizeOfPartitionEntry == 0 {
			g.Primary.SizeOfPartitionEntry = PartitionEntrySize
		}

		// the minimium number of partitions is defined by the minimum allowed size
		numPartitions := MinPartitionEntryArraySize / g.Primary.SizeOfPartitionEntry
		if numPartitions < g.Primary.NumberOfPartitionEntries {
			numPartitions = g.Primary.NumberOfPartitionEntries
		}
		g.Primary.Partitions = make([]PartitionEntry, numPartitions)

		pePad := int64(g.Primary.SizeOfPartitionEntry - PartitionEntrySize)
		for i := range g.Primary.Partitions {
			if _, err := g.Primary.Partitions[i].ReadFrom(r); err != nil {
				return g, err
			}

			if _, err := r.Seek(pePad, io.SeekCurrent); err != nil {
				return g, err
			}
		}
	}

	lastBlock := int64(diskSize - uint64(blockSize))
	if _, err := r.Seek(lastBlock, io.SeekStart); err != nil {
		return g, err
	}

	if _, err := g.Backup.ReadFrom(r); err != nil {
		return g, err
	}

	if g.Backup.ComputeHeaderCRC32() == g.Backup.HeaderCRC32 {
		ptLoc := int64(g.Backup.PartitionEntryLBA * uint64(blockSize))
		if _, err := r.Seek(ptLoc, io.SeekStart); err != nil {
			return g, err
		}

		// Avoid divide by zero, recover any possibly readable data..
		if g.Backup.SizeOfPartitionEntry == 0 {
			g.Backup.SizeOfPartitionEntry = PartitionEntrySize
		}

		numPartitions := MinPartitionEntryArraySize / g.Backup.SizeOfPartitionEntry
		if numPartitions < g.Backup.NumberOfPartitionEntries {
			numPartitions = g.Backup.NumberOfPartitionEntries
		}
		g.Backup.Partitions = make([]PartitionEntry, numPartitions)

		pePad := int64(g.Backup.SizeOfPartitionEntry - PartitionEntrySize)
		for i := range g.Backup.Partitions {
			if _, err := g.Backup.Partitions[i].ReadFrom(r); err != nil {
				return g, err
			}

			if _, err := r.Seek(pePad, io.SeekCurrent); err != nil {
				return g, err
			}
		}
	}

	return g, nil
}

// nullReader is a convenience for cross-platform io.copy of 0 bytes to a target
// writer.
type nullReader struct {}

func (nr nullReader) Read(b []byte) (int, error) {
	for i := range b {
		b[i] = 0
	}
	return len(b), nil
}

func seekOrWrite(w io.Writer, n int64) (int64, error) {
	if seeker, canSeek := w.(io.Seeker); canSeek {
		_, err := seeker.Seek(n, io.SeekCurrent)
		return 0, err
	}
	return io.CopyN(w, nullReader{}, n)
}

// WriteTo implements io.WriterTo for writing out the GPT to a target writer. It
// expects that update was called with correct values beforehand, but it will,
// if otherwise un-set try to detect reasonable values for logical and physical
// block sizes. If the target writer does not support detection of these sizes,
// and they were not supplied to Update, the write will make conventional
// assumptions (logical block size: 512, physical block size: 4096). Note: If
// the given writer does not implement io.Seeker, then the write will zero out
// all non-partition data regions. If the argument implements io.Seeker, then
// all non-partition table data will remain untouched.
func (g *GPT) WriteTo(w io.Writer) (int64, error) {
	if f, ok := w.(*os.File); ok {
		if g.logical == 0 {
			g.logical, _ = GetLogicalBlockSize(f)
		}
		if g.physical == 0 {
			g.physical, _ = GetPhysicalBlockSize(f)
		}
		// TODO(raggi): try to find out optimal transfer sizes
	}
	if g.logical == 0 {
		g.logical = FallbackLogicalBlockSize
	}
	if g.physical == 0 {
		g.physical = FallbackPhysicalBlockSize
	}

	var total int64

	if seeker, canSeek := w.(io.Seeker); canSeek {
		if _, err := seeker.Seek(0, io.SeekStart); err != nil {
			return total, err
		}
	}

	// MBR...
	n, err := g.MBR.WriteTo(w)
	total += n
	if err != nil {
		return total, err
	}

	// write zero's up to the second block
	n, err = io.CopyN(w, nullReader{}, total-int64(g.logical))
	total += n
	if err != nil {
		return total, err
	}

	// Primary...
	n, err = g.Primary.Header.WriteTo(w)
	total += n
	if err != nil {
		return total, err
	}

	if total%int64(g.logical) != 0 {
		n, err = io.CopyN(w, nullReader{}, int64(g.logical)-total%int64(g.logical))
		total += n
		if err != nil {
			return total, err
		}
	}

	n, err = g.Primary.Partitions.WriteTo(w)
	total += n
	if err != nil {
		return total, err
	}

	n, err = io.CopyN(w, nullReader{}, g.Primary.PartitionArrayPad())
	total += n
	if err != nil {
		return total, err
	}

	// Backup...

	n, err = seekOrWrite(w, int64(g.Backup.PartitionEntryLBA*g.logical)-total)
	total += n
	if err != nil {
		return total, err
	}

	n, err = g.Backup.Partitions.WriteTo(w)
	total += n
	if err != nil {
		return total, err
	}

	n, err = io.CopyN(w, nullReader{}, g.Primary.PartitionArrayPad())
	total += n
	if err != nil {
		return total, err
	}
	if total%int64(g.logical) != 0 {
		n, err = io.CopyN(w, nullReader{}, int64(g.logical)-total%int64(g.logical))
		total += n
		if err != nil {
			return total, err
		}
	}

	n, err = g.Backup.Header.WriteTo(w)
	total += n
	if err != nil {
		return total, err
	}
	if total%int64(g.logical) != 0 {
		n, err = io.CopyN(w, nullReader{}, int64(g.logical)-total%int64(g.logical))
		total += n
		if err != nil {
			return total, err
		}
	}

	return total, err
}

func (g GPT) String() string {
	var b bytes.Buffer
	fmt.Fprintf(&b, "MBR:\n%s\n\n", g.MBR)
	if g.Primary.Signature == EFISignature {
		fmt.Fprintf(&b, "GPT Primary:\n%s\n\n", g.Primary)
	}
	if g.Backup.Signature == EFISignature {
		fmt.Fprintf(&b, "GPT Backup:\n%s\n\n", g.Backup)
	}
	fmt.Fprintf(&b, "GPT Valid: %v\n", g.Validate() == nil)
	return b.String()
}

// Update uses the provided geometry information, combined with the values
// already present in g to update the geometry and CRC fields for the tables and
// partitions in g. If the changes make laying out the partitions impossible, an
// error is returned. Subsequent calls to WriteTo will use the given values if
// non-zero.
func (g *GPT) Update(blockSize, physicalBlockSize, optimalTransferLengthGranularity, diskSize uint64) error {
	g.logical = blockSize
	g.physical = physicalBlockSize
	g.optimal = optimalTransferLengthGranularity
	g.size = diskSize

	// Various fields in the GPT have known constant values, they're setup first
	g.Primary.Signature = EFISignature
	g.Primary.Revision = EFIRevision
	g.Primary.HeaderSize = HeaderSize
	g.Primary.Reserved = [4]byte{}
	g.Primary.SizeOfPartitionEntry = PartitionEntrySize

	// The primary partition array always starts at logical block 2
	g.Primary.PartitionEntryLBA = 2

	// The number of non-zero partition entries
	paSize := uint32(len(g.Primary.Partitions)) * g.Primary.SizeOfPartitionEntry
	if paSize < MinPartitionEntryArraySize {
		paSize = MinPartitionEntryArraySize
	}
	g.Primary.NumberOfPartitionEntries = uint32(paSize / g.Primary.SizeOfPartitionEntry)

	// The first usable block is located after the end of the partition array
	ptBlockSize := uint64(paSize) / blockSize
	diskBlockSize := diskSize / blockSize
	headerBlockSize := uint64(g.Primary.HeaderSize)
	if headerBlockSize < blockSize {
		headerBlockSize = blockSize
	}
	headerBlockSize = headerBlockSize / blockSize

	g.Primary.MyLBA = 1
	g.Primary.FirstUsableLBA = 2 + ptBlockSize
	g.Primary.LastUsableLBA = diskBlockSize - ptBlockSize - uint64(headerBlockSize) - 1
	g.Primary.AlternateLBA = diskBlockSize - 1

	if g.Primary.DiskGUID.IsZero() {
		g.Primary.DiskGUID = NewRandomGUID()
	}

	// GPT partitions should be aligned to the larger of:
	// a) the physical block boundary
	// b) the optimal transfer length granularity
	alignTo := uint64(blockSize)
	if physicalBlockSize > alignTo {
		alignTo = physicalBlockSize
	}
	if optimalTransferLengthGranularity > alignTo {
		alignTo = optimalTransferLengthGranularity
	}
	alignTo = alignTo / uint64(blockSize)

	nextUsableLBA := g.Primary.FirstUsableLBA
	for i := range g.Primary.Partitions {
		var p = &g.Primary.Partitions[i]

		if p.IsZero() {
			continue
		}
		if d := nextUsableLBA % alignTo; d != 0 {
			nextUsableLBA = nextUsableLBA + alignTo - d
		}
		size := p.EndingLBA - p.StartingLBA
		p.StartingLBA = nextUsableLBA
		p.EndingLBA = nextUsableLBA + size
		nextUsableLBA = p.EndingLBA
	}

	// Copy the updated primary data and partition entries to backup
	g.Backup = g.Primary

	// The backup header lives at the last block of the disk
	g.Backup.MyLBA, g.Backup.AlternateLBA = g.Primary.AlternateLBA, g.Primary.MyLBA
	g.Backup.PartitionEntryLBA = g.Backup.LastUsableLBA + 1

	// Update the CRCs
	for _, pt := range []*PartitionTable{&g.Primary, &g.Backup} {
		pt.PartitionEntryArrayCRC32 = pt.ComputePartitionArrayCRC32()
		pt.HeaderCRC32 = pt.ComputeHeaderCRC32()
	}

	return nil
}

// Validate runs a set of validation operations on the entire GPT, and if errors
// are found, returns them inside ErrInvalidGPT
func (g *GPT) Validate() error {
	// TODO(raggi): the errors array might be better off as a struct that
	// separates errors in primary from errors in backup, and then move the
	// contents of this method onto PartitionTable and call once for each.
	var errors ErrInvalidGPT
	if g.Primary.Signature != EFISignature || g.Backup.Signature != EFISignature {
		errors = append(errors, ErrInvalidSignature)
	}
	if g.Primary.MyLBA != 1 || g.Backup.AlternateLBA != 1 {
		errors = append(errors, ErrInvalidAddress)
	}
	if g.Primary.AlternateLBA != g.Backup.MyLBA || g.Backup.AlternateLBA != g.Primary.MyLBA {
		errors = append(errors, ErrInvalidAddress)
	}

	// Note that this also covers the partition table CRC32, as that is part of
	// the calculation.
	if g.Primary.ComputeHeaderCRC32() != g.Primary.HeaderCRC32 ||
		g.Backup.ComputeHeaderCRC32() != g.Backup.HeaderCRC32 {
		errors = append(errors, ErrHeaderCRC)
	}

	// TODO(raggi): although it is not required by EFI for booting, and thus not
	// in the spec section about validation, there are rules about not producing
	// overlapping partitions and over-extended partitions that are not checked
	// here. They should be checked somewhere, but that would require a knowledge
	// of the disk size and other such things.

	if errors == nil {
		return nil
	}
	if len(errors) == 0 {
		return nil
	}
	return errors
}

// FallbackPhysicalBlockSize of 4096 is returned (with error) from
// GetPhysicalBlockSize as a sensible default assumption. Devices are tending
// toward this as a common physical sector size, and there isn't much to lose
// from this alignment as opposed to 512, the prior common value. By contrast,
// write performance can be significantly negatively affected by an alignment
// less than this on such disks.
const FallbackPhysicalBlockSize = 4096

// FallbackLogicalBlockSize of 512 is returned (with error) from
// GetLogicalBlockSize as a sensible default assumption.
const FallbackLogicalBlockSize = 512

const (
	// BLKBSZGET is the linux ioctl flag for physical block size
	BLKBSZGET = 2148012656
	// DKIOCGETPHYSICALBLOCKSIZE is the darwin ioctl flag for physical block size
	DKIOCGETPHYSICALBLOCKSIZE = 1074029645

	// BLKSSZGET is the linux ioctl flag for logical block size
	BLKSSZGET = 4712
	// DKIOCGETBLOCKSIZE is the darwin ioctl flag for logical block size
	DKIOCGETBLOCKSIZE = 1074029592

	// BLKGETSIZE is the linux ioctl flag for getting disk block count
	BLKGETSIZE = 4704
	// DKIOCGETBLOCKCOUNT is the darwin ioctl flag for disk block count
	DKIOCGETBLOCKCOUNT = 1074291737
)

// GetPhysicalBlockSize fetches the physical block size of the given file. It
// requires elevated process priviliges to execute on most platforms. Currently
// only supported on Linux and Darwin.
func GetPhysicalBlockSize(f *os.File) (uint64, error) {
	var ioctl uintptr
	switch runtime.GOOS {
	case "linux":
		ioctl = BLKBSZGET
	case "darwin":
		ioctl = DKIOCGETPHYSICALBLOCKSIZE
	default:
		return FallbackPhysicalBlockSize, ErrUnsupportedPlatform
	}

	var sz uint32
	var err error
	_, _, er := syscall.Syscall(syscall.SYS_IOCTL, uintptr(f.Fd()), ioctl, uintptr(unsafe.Pointer(&sz)))
	if er != 0 {
		err = os.NewSyscallError("ioctl", er)
		sz = FallbackPhysicalBlockSize
	}
	return uint64(sz), err
}

// GetLogicalBlockSize fetches the physical block size of the given file. It
// requires elevated process priviliges to execute on most platforms. Currently
// only supported on Linux and Darwin.
func GetLogicalBlockSize(f *os.File) (uint64, error) {
	var ioctl uintptr
	switch runtime.GOOS {
	case "linux":
		ioctl = BLKSSZGET
	case "darwin":
		ioctl = DKIOCGETBLOCKSIZE
	default:
		return FallbackLogicalBlockSize, ErrUnsupportedPlatform
	}

	var sz uint32
	var err error
	_, _, er := syscall.Syscall(syscall.SYS_IOCTL, uintptr(f.Fd()), ioctl, uintptr(unsafe.Pointer(&sz)))
	if er != 0 {
		err = os.NewSyscallError("ioctl", er)
		sz = FallbackLogicalBlockSize
	}
	return uint64(sz), err
}

// GetDiskSize fetches the byte size of the given disk.
func GetDiskSize(f *os.File) (uint64, error) {
	var ioctl uintptr
	switch runtime.GOOS {
	case "linux":
		ioctl = BLKGETSIZE
	case "darwin":
		ioctl = DKIOCGETBLOCKCOUNT
	default:
		return 0, ErrUnsupportedPlatform
	}

	var sz uint64
	var err error
	_, _, er := syscall.Syscall(syscall.SYS_IOCTL, uintptr(f.Fd()), ioctl, uintptr(unsafe.Pointer(&sz)))
	if er != 0 {
		err = os.NewSyscallError("ioctl", er)
	}

	lbs, err := GetLogicalBlockSize(f)
	if err != nil {
		return 0, err
	}

	return uint64(lbs) * sz, err
}
