// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"runtime"
	"strconv"

	"go.fuchsia.dev/fuchsia/src/lib/thinfs/block/file"
	"go.fuchsia.dev/fuchsia/src/lib/thinfs/fs"
	"go.fuchsia.dev/fuchsia/src/lib/thinfs/fs/msdosfs"
	"go.fuchsia.dev/fuchsia/src/lib/thinfs/gpt"
	"go.fuchsia.dev/fuchsia/src/lib/thinfs/mbr"
)

// Represents a disk.
type diskInfo struct {
	// Physical block size, in bytes.
	physical uint64
	// Logical block size, in bytes.
	logical uint64
	// Optimal transfer size, in bytes.
	optimal uint64
	// Disk size, in bytes.
	diskSize uint64
	// GPT for this disk.
	gpt gpt.GPT
}

// Represents the size of each partition.
type partitionSizes struct {
	efiSize    uint64
	abrSize    uint64
	vbmetaSize uint64
	fvmSize    uint64
}

// Represents a partition layout on a disk.
type partitionLayout struct {
	// all of these are LBAs
	efiStart     uint64
	aStart       uint64
	vbmetaAStart uint64
	bStart       uint64
	vbmetaBStart uint64
	rStart       uint64
	vbmetaRStart uint64
	miscStart    uint64
	fvmStart     uint64
}

func prepareDisk(disk string) diskInfo {
	if *resize != 0 {
		s, err := os.Stat(disk)
		if err == nil {
			if s.Size() != *resize {
				if *verbose {
					log.Printf("Resizing %q from %d to %d", disk, s.Size(), *resize)
				}
				if err := os.Truncate(disk, *resize); err != nil {
					log.Fatalf("failed to truncate %q to %d: %s", disk, *resize, err)
				}
			}
		} else if os.IsNotExist(err) {
			if *verbose {
				log.Printf("Creating %q", disk)
			}
			f, err := os.Create(disk)
			if err != nil {
				log.Fatalf("failed to create %q: %s", disk, err)
			}
			if err := f.Truncate(*resize); err != nil {
				log.Fatalf("failed to truncate %q to %d: %s", disk, *resize, err)
			}
			f.Close()
		} else {
			log.Fatal(err)
		}
	} else {
		if _, err := os.Stat(disk); err != nil {
			log.Fatalf("cannot read %q: %s\n", disk, err)
		}
	}

	f, err := os.Open(disk)
	if err != nil {
		log.Fatal(err)
	}

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

	// ignore the error here as it may be an image file...
	diskSize, _ := gpt.GetDiskSize(f)

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

	// Note: this isn't entirely correct, as it doesn't take into account padding.
	// Consider adding a real API for this in the GPT lib.
	minGPTSize := int64((gpt.MinPartitionEntryArraySize + gpt.HeaderSize) * 2)
	if uint64(*efiSize+minGPTSize) > diskSize {
		log.Fatalf("%q is not large enough for the partition layout\n", disk)
	}

	if *verbose {
		log.Printf("Disk: %s", disk)
		log.Printf("Disk size: %d", diskSize)
		log.Printf("Block Size: %d", blockSize)
		log.Printf("Physical block size: %d", physicalBlockSize)
		log.Printf("Optimal transfer size: %d", *optimalTransferSize)
	}

	g, err := gpt.ReadGPT(f, uint64(*blockSize), diskSize)
	if err != nil {
		log.Fatal(err)
	}

	return diskInfo{
		physical: uint64(*physicalBlockSize),
		logical:  uint64(*blockSize),
		optimal:  uint64(*optimalTransferSize),
		diskSize: diskSize,
		gpt:      g,
	}
}

func createPartitionTable(disk *diskInfo, sizes *partitionSizes, abr, verbose, ramdiskOnly, sparseFvm bool) partitionLayout {
	lbaSize := disk.diskSize / disk.logical
	disk.gpt.MBR = mbr.NewProtectiveMBR(lbaSize)
	disk.gpt.Primary.Partitions = []gpt.PartitionEntry{}

	disk.gpt.Update(disk.logical, disk.physical, disk.optimal, disk.diskSize) // for the firstusablelba
	end := disk.gpt.Primary.FirstUsableLBA

	var efiStart uint64
	efiStart, end = optimalBlockAlign(end, sizes.efiSize, disk.logical, disk.physical, disk.optimal)
	// compute the size of the fat geometry that fits within the well-aligned GPT
	// partition that was computed above.
	efiSize := uint64(fitFAT(int64((end)-efiStart) * int64(disk.logical)))
	// efiEnd is the last sector of viable fat geometry, which may be different
	// from end, which is the last sector of the gpt partition.
	efiEnd := efiStart + (efiSize / disk.logical) - 1

	if verbose {
		log.Printf("EFI START: %d", efiStart)
		log.Printf("EFI END: %d", efiEnd)
		log.Printf("EFI LB SIZE: %d", efiEnd-efiStart+1)
	}

	disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
		PartitionTypeGUID:   gpt.GUIDEFI,
		UniquePartitionGUID: gpt.NewRandomGUID(),
		PartitionName:       gpt.NewPartitionName("efi-system"),
		StartingLBA:         efiStart,
		EndingLBA:           end,
	})

	var startingCHS [3]byte
	startingCHS[0] = byte(efiStart / (16 * 63))
	startingCHS[1] = byte((efiStart / 63) % 16)
	startingCHS[2] = byte((efiStart % 63) + 1)

	var endingCHS [3]byte
	endingCHS[0] = byte(efiEnd / (16 * 63))
	endingCHS[1] = byte((efiEnd / 63) % 16)
	endingCHS[2] = byte((efiEnd % 63) + 1)

	// Install a "hybrid MBR" hack for the case of bios bootloaders that might
	// need it (e.g. rpi's binary blob that's stuck in MBR land).
	disk.gpt.MBR.PartitionRecord[2] = mbr.PartitionRecord{
		BootIndicator: 0x80,
		StartingCHS:   startingCHS,
		EndingCHS:     endingCHS,
		OSType:        mbr.FAT32,
		StartingLBA:   uint32(efiStart),
		SizeInLBA:     uint32(efiEnd),
	}

	var aStart, bStart, rStart uint64
	var vbmetaAStart, vbmetaBStart, vbmetaRStart, miscStart uint64
	if abr {
		aStart, end = optimalBlockAlign(end+1, uint64(sizes.abrSize), disk.logical, disk.physical, disk.optimal)
		disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaZirconA,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("ZIRCON-A"),
			StartingLBA:         aStart,
			EndingLBA:           end,
		})

		vbmetaAStart, end = optimalBlockAlign(end+1, uint64(sizes.vbmetaSize), disk.logical, disk.physical, disk.optimal)
		disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaVbmetaA,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("VBMETA_A"),
			StartingLBA:         vbmetaAStart,
			EndingLBA:           end,
		})

		bStart, end = optimalBlockAlign(end+1, uint64(sizes.abrSize), disk.logical, disk.physical, disk.optimal)
		disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaZirconB,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("ZIRCON-B"),
			StartingLBA:         bStart,
			EndingLBA:           end,
		})

		vbmetaBStart, end = optimalBlockAlign(end+1, uint64(sizes.vbmetaSize), disk.logical, disk.physical, disk.optimal)
		disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaVbmetaB,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("VBMETA_B"),
			StartingLBA:         vbmetaBStart,
			EndingLBA:           end,
		})

		rStart, end = optimalBlockAlign(end+1, uint64(sizes.abrSize), disk.logical, disk.physical, disk.optimal)
		disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaZirconR,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("ZIRCON-R"),
			StartingLBA:         rStart,
			EndingLBA:           end,
		})

		vbmetaRStart, end = optimalBlockAlign(end+1, uint64(sizes.vbmetaSize), disk.logical, disk.physical, disk.optimal)
		disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaVbmetaR,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("VBMETA_R"),
			StartingLBA:         vbmetaRStart,
			EndingLBA:           end,
		})

		miscStart, end = optimalBlockAlign(end+1, uint64(sizes.vbmetaSize), disk.logical, disk.physical, disk.optimal)
		disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaMisc,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("MISC"),
			StartingLBA:         miscStart,
			EndingLBA:           end,
		})
	}

	var fvmStart uint64

	fvmStart, end = optimalBlockAlign(end+1, uint64(sizes.fvmSize), disk.logical, disk.physical, disk.optimal)
	if !ramdiskOnly {
		if sizes.fvmSize == 0 {
			end = disk.gpt.Primary.LastUsableLBA
		}
		sizes.fvmSize = (end + 1 - fvmStart) * disk.logical

		guid := gpt.GUIDFuchsiaFVM
		name := gpt.NewPartitionName("FVM")
		if sparseFvm {
			guid = gpt.GUIDFuchsiaInstaller
			name = gpt.NewPartitionName("storage-sparse")
		}

		disk.gpt.Primary.Partitions = append(disk.gpt.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   guid,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       name,
			StartingLBA:         fvmStart,
			EndingLBA:           end,
		})
	}

	disk.gpt.Update(disk.logical, disk.physical, disk.optimal, disk.diskSize)

	if err := disk.gpt.Validate(); err != nil {
		log.Fatal(err)
	}

	if verbose {
		log.Printf("EFI size: %d", sizes.efiSize)
		if !ramdiskOnly {
			log.Printf("FVM size: %d", sizes.fvmSize)
		}

		log.Printf("Writing GPT")
	}

	return partitionLayout{
		// all of these are LBAs
		aStart:       aStart,
		vbmetaAStart: vbmetaAStart,
		bStart:       bStart,
		vbmetaBStart: vbmetaBStart,
		rStart:       rStart,
		vbmetaRStart: vbmetaRStart,
		miscStart:    miscStart,
		efiStart:     efiStart,
		fvmStart:     fvmStart,
	}
}

func writeDisk(disk string, partitions partitionLayout, diskInfo diskInfo, sizes partitionSizes, bootMode BootPartition) {
	f, err := os.OpenFile(disk, os.O_RDWR, 0750)
	if err != nil {
		log.Fatal(err)
	}
	if _, err := diskInfo.gpt.WriteTo(f); err != nil {
		log.Fatalf("error writing partition table: %s", err)
	}

	f.Sync()

	aStart := partitions.aStart * diskInfo.logical
	vbmetaAStart := partitions.vbmetaAStart * diskInfo.logical
	bStart := partitions.bStart * diskInfo.logical
	vbmetaBStart := partitions.vbmetaBStart * diskInfo.logical
	rStart := partitions.rStart * diskInfo.logical
	vbmetaRStart := partitions.vbmetaRStart * diskInfo.logical
	miscStart := partitions.miscStart * diskInfo.logical
	efiStart := partitions.efiStart * diskInfo.logical
	fvmStart := partitions.fvmStart * diskInfo.logical

	if *verbose {
		log.Printf("Writing EFI partition and files")
	}

	cmd := exec.Command(fuchsiaTool("mkfs-msdosfs"),
		"-@", strconv.FormatUint(efiStart, 10),
		// XXX(raggi): mkfs-msdosfs offset gets subtracted by the tool for available
		// size, so we have to add the offset back on to get the correct geometry.
		"-S", strconv.FormatUint(sizes.efiSize+efiStart, 10),
		"-F", "32",
		"-L", "ESP",
		"-O", "Fuchsia",
		"-b", fmt.Sprintf("%d", diskInfo.logical),
		disk,
	)

	if out, err := cmd.CombinedOutput(); err != nil {
		log.Printf("mkfs-msdosfs failed:\n%s", out)
		log.Fatal(err)
	}

	dev, err := file.NewRange(f, int64(diskInfo.logical), int64(efiStart), int64(sizes.efiSize))
	if err != nil {
		log.Fatal(err)
	}

	fatfs, err := msdosfs.New(disk, dev, fs.ReadWrite|fs.Force)
	if err != nil {
		log.Fatal(err)
	}

	root := fatfs.RootDirectory()

	tf, err := ioutil.TempFile("", "gsetup-boot")
	if err != nil {
		log.Fatal(err)
	}
	tf.WriteString("efi\\boot\\bootx64.efi")
	tf.Close()
	defer os.Remove(tf.Name())

	msCopyIn(root, tf.Name(), "EFI/Google/GSetup/Boot")
	msCopyIn(root, *bootloader, "EFI/BOOT/bootx64.efi")
	if !*abr {
		msCopyIn(root, *zbi, "zircon.bin")
		msCopyIn(root, *zedboot, "zedboot.bin")
	}
	if *cmdline != "" {
		msCopyIn(root, *cmdline, "cmdline")
	}

	root.Sync()
	if err := root.Close(); err != nil {
		log.Fatal(err)
	}
	if err := fatfs.Close(); err != nil {
		log.Fatal(err)
	}

	f.Sync()

	if *abr {
		if *verbose {
			log.Print("Populating A/B/R and vbmeta partitions")
		}
		partitionCopy(f, int64(aStart), *abrSize, *zirconA)
		partitionCopy(f, int64(vbmetaAStart), *vbmetaSize, *vbmetaA)
		partitionCopy(f, int64(bStart), *abrSize, *zirconB)
		partitionCopy(f, int64(vbmetaBStart), *vbmetaSize, *vbmetaB)
		partitionCopy(f, int64(rStart), *abrSize, *zirconR)
		partitionCopy(f, int64(vbmetaRStart), *vbmetaSize, *vbmetaR)
		if _, err := f.Seek(int64(miscStart), os.SEEK_SET); err != nil {
			log.Fatal(err)
		}
		if err := WriteAbr(bootMode, f); err != nil {
			log.Fatal(err)
		}

	}

	f.Sync()

	if !*ramdiskOnly {
		if *verbose {
			log.Print("Populating FVM in GPT image")
		}
		if *useSparseFvm {
			partitionCopy(f, int64(fvmStart), *fvmSize, *sparseFvm)
		} else {
			slice_size := strconv.FormatInt(8*(1<<20), 10)
			fvm(disk, int64(fvmStart), int64(sizes.fvmSize), "create",
				"--slice", slice_size, "--blob", *blob, "--data", *data)
		}
	}

	// Keep the file open so that OSX doesn't try to remount the disk while tools are working on it.
	if err := f.Close(); err != nil {
		log.Fatal(err)
	}

	if *verbose {
		log.Printf("Done")
	}
}
