// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// make-fuchsia-vol is a temporarily useful script that provisions Fuchsia
// volumes based on paths provided.

package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"

	"thinfs/block/file"
	"thinfs/fs"
	"thinfs/fs/msdosfs"
	"thinfs/gpt"
	"thinfs/mbr"
)

var (
	fuchsiaOutDir   = flag.String("fuchsia-out-dir", os.Getenv("FUCHSIA_OUT_DIR"), "fuchsia out dir")
	fuchsiaBuildDir = flag.String("fuchsia-build-dir", os.Getenv("FUCHSIA_BUILD_DIR"), "fuchsia build dir")
	zirconBuildDir  = flag.String("zircon-build-dir", os.Getenv("ZIRCON_BUILD_DIR"), "zircon build dir")
	zirconToolsDir  = flag.String("zircon-tools-dir", os.Getenv("ZIRCON_TOOLS_DIR"), "zircon tools dir")

	bootloader = flag.String("bootloader", "", "path to bootx64.efi")
	kernel     = flag.String("kernel", "", "path to zircon.bin")
	ramdisk    = flag.String("ramdisk", "", "path to ramdisk.bin")
	cmdline    = flag.String("cmdline", "", "path to command line file (if exists)")
	zedboot    = flag.String("zedboot", "", "path to zedboot.bin")

	grub        = flag.Bool("grub", false, "install grub to the disk")
	grubBootImg = flag.String("grub-mbr", "", "path to grub mbr image")
	grubCoreImg = flag.String("grub-core", "", "path to grub standalone core.img")

	ramdiskOnly = flag.Bool("ramdisk-only", false, "ramdisk-only mode - only write an ESP partition")
	blob        = flag.String("blob", "", "path to blob partition image (not used with ramdisk)")
	data        = flag.String("data", "", "path to data partition image (not used with ramdisk)")

	abr     = flag.Bool("abr", false, "add Zircon-{A,B,R} partitions")
	zirconA = flag.String("zirconA", "", "path to partition image for Zircon-A")
	zirconB = flag.String("zirconB", "", "path to partition image for Zircon-B")
	zirconR = flag.String("zirconR", "", "path to partition image for Zircon-R")
	abrSize = flag.Int64("abr-size", 16*1024*1024, "Kernel partition size for A/B/R")

	board = flag.String("board", "pc", "Zircon board name to use/build for")

	blockSize           = flag.Int64("block-size", 0, "the block size of the target disk (0 means detect)")
	physicalBlockSize   = flag.Int64("physical-block-size", 0, "the physical block size of the target disk (0 means detect)")
	optimalTransferSize = flag.Int64("optimal-transfer-size", 0, "the optimal transfer size of the target disk (0 means unknown/unused)")

	efiSize = flag.Int64("efi-size", 63*1024*1024, "efi partition size in bytes")
	fvmSize = flag.Int64("fvm-size", 0, "fvm partition size in bytes (0 means `fill`)")
)

const grubCoreOffset = 92

// imageManifests contains a list of known manifests produced by the build that contains build-dir relative paths to images.
var imageManifests = []string{"zedboot_image_paths.sh", "image_paths.sh"}

// imagePaths contains the default image paths that are produced by a build manifest, populated by tryLoadManifests.
var imagePaths = map[string]string{}

// getImage fetches an image by name or exits fatally
func getImage(name string) string {
	if path, ok := imagePaths[name]; ok {
		return path
	}
	log.Fatalf("Missing image path: %q cannot continue", name)
	return ""
}

func init() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s disk-path\n", filepath.Base(os.Args[0]))
		flag.PrintDefaults()
	}
}

func tryLoadManifests() {
	if *fuchsiaBuildDir != "" {
		for _, manifest := range imageManifests {
			content, err := ioutil.ReadFile(filepath.Join(*fuchsiaBuildDir, manifest))
			if err != nil {
				log.Printf("warning: failed to load %s manifest: %s", manifest, err)
				continue
			}
			lines := strings.Split(string(content), "\n")
			for _, line := range lines {
				line = strings.TrimSpace(line)
				if line == "" {
					continue
				}
				parts := strings.SplitN(line, "=", 2)
				if len(parts) != 2 {
					log.Fatalf("Failed to parse %s: line %q contained incorrect number of `=`", manifest, line)
				}
				imagePaths[parts[0]] = parts[1]
			}
		}
	}
}

func needFuchsiaOutDir() {
	if *fuchsiaOutDir == "" {
		log.Fatalf("either pass -fuchsia-out-dir or set $FUCHSIA_OUT_DIR")
	}
}

func needFuchsiaBuildDir() {
	if *fuchsiaBuildDir == "" {
		log.Fatalf("either pass -fuchsia-build-dir or set $FUCHSIA_BUILD_DIR")
	}
}

func needZirconBuildDir() {
	if *zirconBuildDir == "" {
		log.Fatalf("either pass -zircon-build-dir or set $ZIRCON_BUILD_DIR")
	}
}

func needZirconToolsDir() {
	if *zirconToolsDir == "" {
		log.Fatalf("either pass -zircon-tools-dir or set $ZIRCON_TOOLS_DIR")
	}
}

func main() {
	flag.Parse()
	tryLoadManifests()

	if *bootloader == "" {
		needZirconBuildDir()
		*bootloader = filepath.Join(*zirconBuildDir, "bootloader/bootx64.efi")
	}
	if *kernel == "" {
		needZirconBuildDir()
		*kernel = filepath.Join(*zirconBuildDir, "zircon.bin")
	}
	if *ramdisk == "" {
		needFuchsiaBuildDir()
		if *ramdiskOnly {
			*ramdisk = filepath.Join(*fuchsiaBuildDir, getImage("IMAGE_NETBOOT_RAM"))
		} else {
			*ramdisk = filepath.Join(*fuchsiaBuildDir, getImage("IMAGE_BOOT_RAM"))
		}
	}
	if *cmdline == "" {
		needFuchsiaBuildDir()
		*cmdline = filepath.Join(*fuchsiaBuildDir, "cmdline")
	}

	if *grub {
		if *grubBootImg == "" {
			needFuchsiaOutDir()

			*grubBootImg = filepath.Join(*fuchsiaOutDir, "build-grub/lib/grub/i386-pc/boot.img")
			if _, err := os.Stat(*grubBootImg); err != nil {
				log.Fatalf("%q not found, you may need to run scripts/grubdisk/build-all.sh", *grubBootImg)
			}
		}
		if *grubCoreImg == "" {
			needFuchsiaOutDir()
			*grubCoreImg = filepath.Join(*fuchsiaOutDir, "build-grub/core.img")
			if _, err := os.Stat(*grubCoreImg); err != nil {
				log.Fatalf("%q not found, you may need to run scripts/grubdisk/build-all.sh", *grubCoreImg)
			}
		}
	}

	if *abr {
		if *zirconA == "" {
			needFuchsiaBuildDir()
			*zirconA = filepath.Join(*fuchsiaBuildDir, getImage("IMAGE_ZIRCONA_ZBI"))
		}
		if *zirconB == "" {
			*zirconB = *zirconA
		}
		if *zirconR == "" {
			*zirconR = filepath.Join(*fuchsiaBuildDir, getImage("IMAGE_ZIRCONR_ZBI"))
		}
	}

	if *zedboot == "" {
		needFuchsiaBuildDir()
		*zedboot = filepath.Join(*fuchsiaBuildDir, getImage("IMAGE_ZEDBOOT_ZBI"))
	}

	if !*ramdiskOnly {
		if *blob == "" {
			needFuchsiaBuildDir()
			*blob = filepath.Join(*fuchsiaBuildDir, getImage("IMAGE_BLOB_RAW"))
		}
		if *data == "" {
			needFuchsiaBuildDir()
			*data = filepath.Join(*fuchsiaBuildDir, getImage("IMAGE_DATA_RAW"))
		}

		if _, err := os.Stat(*blob); err != nil {
			log.Fatalf("Blob image error: %s\nEither provide a blob image, or pass -ramdisk", err)
		}
		if _, err := os.Stat(*data); err != nil {
			f, err := os.Create(*data)
			if err != nil {
				log.Fatal(err)
			}
			f.Truncate(10 * 1024 * 1024)
			f.Close()
			if err := exec.Command(zirconTool("minfs"), *data, "create").Run(); err != nil {
				log.Fatalf("minfs %q create failed", *data)
			}
		}
	}

	if len(flag.Args()) != 1 {
		flag.Usage()
		os.Exit(1)
	}

	disk, err := filepath.Abs(flag.Args()[0])
	if err != nil {
		log.Fatal(err)
	}

	for _, path := range []string{*kernel, *ramdisk, disk} {
		if _, err := os.Stat(path); err != nil {
			log.Fatalf("cannot read %q: %s\n", path, err)
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

	// Note: this isn't entirely correct, as it doesn't take into account padding.
	// Consider adding a real API for this in the GPT lib.
	minGPTSize := int64((gpt.MinPartitionEntryArraySize + gpt.HeaderSize) * 2)
	if uint64(*efiSize+minGPTSize) > diskSize {
		log.Fatalf("%q is not large enough for the partition layout\n", disk)
	}

	log.Printf("Disk: %s", disk)
	log.Printf("Disk size: %d", diskSize)
	log.Printf("Block Size: %d", logical)
	log.Printf("Physical block size: %d", physical)
	log.Printf("Optimal transfer size: %d", optimal)

	g, err := gpt.ReadGPT(f, logical, diskSize)
	if err != nil {
		log.Fatal(err)
	}

	lbaSize := diskSize / logical
	g.MBR = mbr.NewProtectiveMBR(lbaSize)
	if *grub {
		grubMBR, err := ioutil.ReadFile(*grubBootImg)
		if err != nil {
			log.Fatal(err)
		}

		copy(g.MBR.BootCode[:], grubMBR[0:len(g.MBR.BootCode)])
		copy(g.MBR.Pad[:], grubMBR[len(g.MBR.BootCode):len(g.MBR.BootCode)+len(g.MBR.Pad)])
	}

	g.Primary.Partitions = []gpt.PartitionEntry{}

	g.Update(logical, physical, optimal, diskSize) // for the firstusablelba
	end := g.Primary.FirstUsableLBA

	var biosStart uint64
	var grubCore []byte
	if *grub {
		var err error
		grubCore, err = ioutil.ReadFile(*grubCoreImg)
		if err != nil {
			log.Fatal(err)
		}
		biosStart, end = optimialBlockAlign(end, uint64(len(grubCore)), logical, physical, optimal)
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
		biosSize := int64((end - biosStart) * logical)

		var startingCHS [3]byte
		startingCHS[0] = byte(biosStart / (16 * 63))
		startingCHS[1] = byte((biosStart / 63) % 16)
		startingCHS[2] = byte((biosStart % 63) + 1)

		var endingCHS [3]byte
		endingCHS[0] = byte(end / (16 * 63))
		endingCHS[1] = byte((end / 63) % 16)
		endingCHS[2] = byte((end % 63) + 1)

		// Install a "hybrid MBR" hack for the case of bios bootloaders that might
		// need it (e.g. rpi's binary blob that's stuck in MBR land).
		g.MBR.PartitionRecord[1] = mbr.PartitionRecord{
			BootIndicator: 0x00,
			StartingCHS:   startingCHS,
			EndingCHS:     endingCHS,
			OSType:        mbr.FAT32,
			StartingLBA:   uint32(biosStart),
			SizeInLBA:     uint32(uint64(biosSize) / logical),
		}
	}

	var efiStart uint64
	efiStart, end = optimialBlockAlign(end, uint64(*efiSize), logical, physical, optimal)
	// compute the size of the fat geometry that fits within the well-aligned GPT
	// partition that was computed above.
	*efiSize = fitFAT(int64((end-1)-efiStart) * int64(logical))
	// efiEnd is the last sector of viable fat geometry, which may be different
	// from end, which is the last sector of the gpt partition.
	efiEnd := efiStart + (uint64(*efiSize) / logical) - 1

	log.Printf("EFI START: %d", efiStart)
	log.Printf("EFI END: %d", efiEnd)
	log.Printf("EFI LB SIZE: %d", efiEnd-efiStart+1)

	g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
		PartitionTypeGUID:   gpt.GUIDEFI,
		UniquePartitionGUID: gpt.NewRandomGUID(),
		PartitionName:       gpt.NewPartitionName("ESP"),
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
	g.MBR.PartitionRecord[2] = mbr.PartitionRecord{
		BootIndicator: 0x80,
		StartingCHS:   startingCHS,
		EndingCHS:     endingCHS,
		OSType:        mbr.FAT32,
		StartingLBA:   uint32(efiStart),
		SizeInLBA:     uint32(efiEnd),
	}

	var aStart, bStart, rStart uint64
	if *abr {
		aStart, end = optimialBlockAlign(end, uint64(*abrSize), logical, physical, optimal)
		g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaZirconA,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("zircon-a"),
			StartingLBA:         aStart,
			EndingLBA:           end,
		})

		bStart, end = optimialBlockAlign(end, uint64(*abrSize), logical, physical, optimal)
		g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaZirconB,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("zircon-b"),
			StartingLBA:         bStart,
			EndingLBA:           end,
		})

		rStart, end = optimialBlockAlign(end, uint64(*abrSize), logical, physical, optimal)
		g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaZirconR,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("zircon-r"),
			StartingLBA:         rStart,
			EndingLBA:           end,
		})
	}

	var fvmStart uint64

	fvmStart, end = optimialBlockAlign(end+1, uint64(*fvmSize), logical, physical, optimal)
	if !*ramdiskOnly {
		if *fvmSize == 0 {
			end = g.Primary.LastUsableLBA
		}
		*fvmSize = int64((end - fvmStart) * logical)

		g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaFVM,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("FVM"),
			StartingLBA:         fvmStart,
			EndingLBA:           end,
		})
	}

	g.Update(logical, physical, optimal, diskSize)

	if err := g.Validate(); err != nil {
		log.Fatal(err)
	}

	log.Printf("EFI size: %d", *efiSize)
	if !*ramdiskOnly {
		log.Printf("FVM size: %d", *fvmSize)
	}

	log.Printf("Writing GPT")

	f, err = os.OpenFile(disk, os.O_RDWR, 0750)
	if err != nil {
		log.Fatal(err)
	}
	if _, err := g.WriteTo(f); err != nil {
		log.Fatalf("error writing partition table: %s", err)
	}

	f.Sync()

	if *grub {
		if _, err := f.Seek(int64(biosStart*logical), io.SeekStart); err != nil {
			log.Fatal(err)
		}

		pad := (int(logical) - (len(grubCore) % int(logical)))
		core := make([]byte, len(grubCore)+pad)
		copy(core, grubCore)

		binary.LittleEndian.PutUint64(core[0x1f4:], biosStart+1)
		sectors := uint16(len(core) / int(logical))
		binary.LittleEndian.PutUint16(core[0x1fc:], sectors)

		if _, err := f.Write(core); err != nil {
			log.Fatal(err)
		}

		f.Sync()
	}

	aStart = aStart * logical
	bStart = bStart * logical
	rStart = rStart * logical
	efiStart = efiStart * logical
	fvmStart = fvmStart * logical

	log.Printf("Writing EFI partition and files")

	cmd := exec.Command(zirconTool("mkfs-msdosfs"),
		"-@", strconv.FormatUint(efiStart, 10),
		// XXX(raggi): mkfs-msdosfs offset gets subtracted by the tool for available
		// size, so we have to add the offset back on to get the correct geometry.
		"-S", strconv.FormatUint(uint64(*efiSize)+efiStart, 10),
		"-F", "32",
		"-L", "ESP",
		"-O", "Fuchsia",
		"-b", fmt.Sprintf("%d", logical),
		disk,
	)

	if out, err := cmd.CombinedOutput(); err != nil {
		log.Printf("mkfs-msdosfs failed:\n%s", out)
		log.Fatal(err)
	}

	dev, err := file.NewRange(f, int64(logical), int64(efiStart), *efiSize)
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
		msCopyIn(root, *kernel, "zircon.bin")
		msCopyIn(root, *ramdisk, "bootdata.bin")
		msCopyIn(root, *zedboot, "zedboot.bin")
	}
	if _, err := os.Stat(*cmdline); err == nil {
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
		log.Print("Populating A/B/R partitions")
		partitionCopy(f, int64(aStart), *abrSize, *zirconA)
		partitionCopy(f, int64(bStart), *abrSize, *zirconB)
		partitionCopy(f, int64(rStart), *abrSize, *zirconR)
	}

	f.Sync()

	if !*ramdiskOnly {
		log.Print("Populating FVM in GPT image")
		fvm(disk, int64(fvmStart), *fvmSize, "create", "--blob", *blob, "--data", *data)
	}

	// Keep the file open so that OSX doesn't try to remount the disk while tools are working on it.
	if err := f.Close(); err != nil {
		log.Fatal(err)
	}

	log.Printf("Done")
}

func partitionCopy(f *os.File, start, size int64, path string) {
	input, err := os.Open(path)
	if err != nil {
		log.Fatalf("partition copy failed for input: %s: %s", path, err)
	}
	defer input.Close()
	r := io.LimitReader(input, size)
	if _, err := f.Seek(start, os.SEEK_SET); err != nil {
		log.Fatalf("partition copy failed for input: %s: %s", path, err)
	}
	_, err = io.Copy(f, r)
	if err != nil {
		log.Fatalf("partition copy failed for input: %s: %s", path, err)
	}
}

func fvm(disk string, offset, size int64, command string, args ...string) {
	offs := strconv.FormatInt(offset, 10)
	szs := strconv.FormatInt(size, 10)
	argv := []string{disk, command, "--offset", offs, "--length", szs}
	argv = append(argv, args...)
	cmd := exec.Command(zirconTool("fvm"), argv...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		log.Fatalf("fvm %s failed", argv)
	}
}

// msCopyIn copies src from the host filesystem into dst under the given
// msdosfs root.
func msCopyIn(root fs.Directory, src, dst string) {
	d := root
	defer d.Sync()

	dStack := []fs.Directory{}

	defer func() {
		for _, d := range dStack {
			d.Sync()
			d.Close()
		}
	}()

	destdir := filepath.Dir(dst)
	name := filepath.Base(dst)

	for _, part := range strings.Split(destdir, "/") {
		if part == "." {
			continue
		}

		var err error
		_, d, _, err = d.Open(part, fs.OpenFlagRead|fs.OpenFlagCreate|fs.OpenFlagDirectory)
		if err != nil {
			log.Fatalf("open/create %s: %#v %s", part, err, err)
		}
		d.Sync()
		dStack = append(dStack, d)
	}

	to, _, _, err := d.Open(name, fs.OpenFlagWrite|fs.OpenFlagCreate|fs.OpenFlagFile)
	if err != nil {
		log.Fatalf("creating %s in msdosfs: %s", name, err)
	}
	defer to.Close()

	from, err := os.Open(src)
	if err != nil {
		log.Fatal(err)
	}
	defer from.Close()

	b := make([]byte, 4096)
	for err == nil {
		var n int
		n, err = from.Read(b)
		if n > 0 {
			if _, err := to.Write(b[:n], 0, fs.WhenceFromCurrent); err != nil {
				log.Fatalf("writing %s to msdosfs file: %s", name, err)
			}
		}
	}
	to.Sync()
	if err != nil && err != io.EOF {
		log.Fatal(err)
	}
}

// optimalBlockAlign computes a start and end logical block address for a
// partition that starts at or after first (block address), of size byteSize,
// for a disk with logical, physical and optimal byte sizes. It returns the
// start and end block addresses.
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

func zirconTool(name string) string {
	var tool string
	tool, _ = exec.LookPath(tool)
	if tool == "" {
		needZirconToolsDir()
		tool, _ = exec.LookPath(filepath.Join(*zirconToolsDir, name))
	}
	if tool == "" {
		log.Fatalf("Could not find %q, you might need to build zircon", name)
	}
	return tool
}

// sectors per track is 63, and a sector is 512, so we must round to the nearest
// 32256.
const sizeAlignment = 32256

// N.B. fitFAT shrinks, not grows, as it intends to identify the nearest
// FAT-compatible geometry that fits inside of "total".
func fitFAT(total int64) int64 {
	if d := total % sizeAlignment; d != 0 {
		total = total - d
	}
	return total
}
