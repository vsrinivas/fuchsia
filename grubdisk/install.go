// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"runtime"

	"thinfs/gpt"
	"thinfs/mbr"
)

const grubCoreOffset = 92

var fuchsiaOutDir = os.Getenv("FUCHSIA_OUT_DIR")

var (
	mbrPath  = flag.String("mbr", filepath.Join(fuchsiaOutDir, "build-grub/lib/grub/i386-pc/boot.img"), "path to grub boot (mbr) image")
	corePath = flag.String("core", filepath.Join(fuchsiaOutDir, "build-grub/core.img"), "path to grub standalone core.img")

	blockSize           = flag.Int64("blockSize", 0, "the block size of the target disk (0 means detect)")
	physicalBlockSize   = flag.Int64("physicalBlockSize", 0, "the physical block size of the target disk (0 means detect)")
	optimalTransferSize = flag.Int64("optimalTransferSize", 0, "the optimal transfer size of the target disk (0 means unknown/unused)")
)

func init() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s disk-path\n", filepath.Base(os.Args[0]))
		flag.PrintDefaults()
	}
}

func check(err error) {
	if err != nil {
		log.Fatal(err)
	}
}

func main() {
	flag.Parse()

	if len(flag.Args()) != 1 {
		flag.Usage()
		os.Exit(1)
	}

	grubMBR, err := ioutil.ReadFile(*mbrPath)
	check(err)
	grubCore, err := ioutil.ReadFile(*corePath)
	check(err)

	disk := flag.Arg(0)

	f, err := os.Open(disk)
	check(err)

	if *blockSize == 0 {
		lbs, err := gpt.GetLogicalBlockSize(f)
		if err != nil {
			log.Printf("WARNING: could not detect logical block size: %s. Assuming %d\n", err, lbs)
		}
		*blockSize = int64(lbs)
	}

	if *physicalBlockSize == 0 {
		pbs, err := gpt.GetPhysicalBlockSize(f)
		if err != nil {
			log.Printf("WARNING: could not detect physical block size: %s. Assuming %d\n", err, pbs)
		}
		*physicalBlockSize = int64(pbs)
	}
	if *physicalBlockSize < 4096 && runtime.GOOS == "darwin" {
		// OSX is not reliably returning correct values for USB sticks, unclear why
		*physicalBlockSize = 4096
	}

	var (
		logical  = uint64(*blockSize)
		physical = uint64(*physicalBlockSize)
		optimal  = uint64(*optimalTransferSize)

		diskSize uint64
	)

	// ignore the error here as it may be an image file...
	diskSize, _ = gpt.GetDiskSize(f)

	if diskSize == 0 {
		s, err := os.Stat(disk)
		if err != nil {
			log.Fatalf("could not stat %q: %s\n", disk, err)
		}
		diskSize = uint64(s.Size())
	}
	if diskSize == 0 {
		log.Fatalf("could not determine size of %q", disk)
	}

	g, err := gpt.ReadGPT(f, logical, diskSize)
	check(err)
	f.Close()

	lbaSize := diskSize / logical
	g.MBR = mbr.NewProtectiveMBR(lbaSize)

	copy(g.MBR.BootCode[:], grubMBR[0:len(g.MBR.BootCode)])
	copy(g.MBR.Pad[:], grubMBR[len(g.MBR.BootCode):len(g.MBR.BootCode)+len(g.MBR.Pad)])

	g.Primary.Partitions = []gpt.PartitionEntry{}

	g.Update(logical, physical, optimal, diskSize) // for the firstusablelba

	// there's no performance reason to align the 2nd stage grub partition,
	// however, not doing so is in violation of UEFI spec and casues issues with
	// other partition & forensics tools.
	biosStart, end := optimialBlockAlign(g.Primary.FirstUsableLBA, uint64(len(grubCore)), logical, physical, optimal)

	binary.LittleEndian.PutUint64(g.MBR.BootCode[grubCoreOffset:], biosStart)

	// nop, nop mr floppy.
	g.MBR.BootCode[0x66] = 0x90
	g.MBR.BootCode[0x67] = 0x90

	g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
		PartitionTypeGUID:   gpt.GUIDBIOS,
		UniquePartitionGUID: gpt.NewRandomGUID(),
		PartitionName:       gpt.NewPartitionName("BIOS"),
		StartingLBA:         biosStart,
		EndingLBA:           end,
	})

	g.Update(logical, physical, optimal, diskSize)

	check(g.Validate())

	f, err = os.OpenFile(disk, os.O_RDWR, 0750)
	check(err)
	if _, err := g.WriteTo(f); err != nil {
		log.Fatalf("error writing partition table: %s", err)
	}

	if _, err := f.Seek(int64(biosStart*logical), io.SeekStart); err != nil {
		check(err)
	}

	pad := (int(logical) - (len(grubCore) % int(logical)))
	core := make([]byte, len(grubCore)+pad)
	copy(core, grubCore)

	binary.LittleEndian.PutUint64(core[0x1f4:], biosStart+1)
	sectors := uint16(len(core) / int(logical))
	binary.LittleEndian.PutUint16(core[0x1fc:], sectors)

	if _, err := f.Write(core); err != nil {
		check(err)
	}

	f.Sync()
	f.Close()
}

func optimialBlockAlign(first, byteSize, logical, physical, optimal uint64) (start, end uint64) {
	var alignTo = logical
	if physical > alignTo {
		alignTo = physical
	}
	if optimal > alignTo {
		alignTo = optimal
	}

	lAlign := alignTo / logical

	if d := first % lAlign; d != 0 {
		start = first + lAlign - d
	}

	lSize := byteSize / logical
	if byteSize%logical == 0 {
		lSize++
	}

	end = start + lSize
	return
}
