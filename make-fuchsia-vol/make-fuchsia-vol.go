// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// make-fuchsia-vol is a temporarily useful script that provisions Fuchsia
// volumes based on paths provided.

package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"thinfs/block/file"
	"thinfs/fs"
	"thinfs/fs/msdosfs"
	"thinfs/gpt"
	"thinfs/mbr"
)

var (
	fuchsiaDir      = os.Getenv("FUCHSIA_DIR")
	fuchsiaBuildDir = os.Getenv("FUCHSIA_BUILD_DIR")
	zirconBuildDir  = os.Getenv("ZIRCON_BUILD_DIR")
)

var (
	bootloader     = flag.String("bootloader", filepath.Join(zirconBuildDir, "bootloader/bootx64.efi"), "path to bootx64.efi")
	kernel         = flag.String("kernel", filepath.Join(zirconBuildDir, "zircon.bin"), "path to zircon.bin")
	bootfsmanifest = flag.String("bootfsmanifest", filepath.Join(zirconBuildDir, "bootfs.manifest"), "path to zircon bootfs.manifest")
	bootmanifest   = flag.String("bootmanifest", filepath.Join(fuchsiaBuildDir, "gen/packages/gn/boot.bootfs.manifest"), "path to boot manifest")
	sysmanifest    = flag.String("sysmanifest", filepath.Join(fuchsiaBuildDir, "gen/packages/gn/system.bootfs.manifest"), "path to system manifest")
	packages       = flag.String("packages", filepath.Join(fuchsiaBuildDir, "gen/packages/gn/packages"), "file containing list of packages to include")
	packagesDir    = flag.String("packagesDir", filepath.Join(fuchsiaBuildDir, "package"), "path to the build packages directory")
	cmdline        = flag.String("cmdline", filepath.Join(fuchsiaBuildDir, "cmdline"), "path to command line file (if exists)")

	rpi3     = flag.Bool("rpi3", strings.Contains(os.Getenv("ZIRCON_PROJECT"), "rpi"), "install rpi3 layout")
	rpi3Root = flag.String("rpi3Root", filepath.Join(fuchsiaDir, "zircon/kernel/target/rpi3"), "zircon rpi3 target root")

	efiOnly = flag.Bool("efionly", false, "install only Zircon EFI partition")

	blockSize           = flag.Int64("blockSize", 0, "the block size of the target disk (0 means detect)")
	physicalBlockSize   = flag.Int64("physicalBlockSize", 0, "the physical block size of the target disk (0 means detect)")
	optimalTransferSize = flag.Int64("optimalTransferSize", 0, "the optimal transfer size of the target disk (0 means unknown/unused)")

	efiSize  = flag.Int64("efiSize", 100*1024*1024, "efi partition size in bytes")
	sysSize  = flag.Int64("sysSize", 2*1024*1024*1024, "sys partition size in bytes")
	blobSize = flag.Int64("blobSize", 1024*1024*1024, "blob partition size in bytes")
	dataSize = flag.Int64("dataSize", 0, "data partition size in bytes (0 means `fill`)")
)

func init() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s disk-path\n", filepath.Base(os.Args[0]))
		flag.PrintDefaults()
	}
}

func main() {
	flag.Parse()

	if len(flag.Args()) != 1 {
		flag.Usage()
		os.Exit(1)
	}

	disk := flag.Args()[0]

	for _, path := range []string{*kernel, *bootfsmanifest, disk} {
		if _, err := os.Stat(path); err != nil {
			log.Fatalf("cannot read %q: %s\n", path, err)
		}
	}

	if !*rpi3 {
		if _, err := os.Stat(*bootloader); err != nil {
			log.Fatalf("cannot read %q: %s\n", *bootloader, err)
		}
	}

	bootManifests := []string{}
	systemManifests := []string{}

	if info, err := os.Stat(*bootmanifest); err == nil {
		if info.Size() > 0 {
			bootManifests = append(bootManifests, *bootmanifest)
		}
	} else {
		log.Printf("boot manifest %s was missing, ignoring", *bootmanifest)
	}

	if info, err := os.Stat(*sysmanifest); err == nil {
		if info.Size() > 0 {
			systemManifests = append(systemManifests, *sysmanifest)
		}
	} else {
		log.Printf("sys manifest %s was missing, ignoring", *sysmanifest)
	}

	if !*efiOnly {
		b, err := ioutil.ReadFile(*packages)
		if err == nil {
			for _, packageName := range strings.Split(string(b), "\n") {
				path := filepath.Join(*packagesDir, packageName, "system_manifest")
				// TODO(raggi): remove the size check hack here once packages declare
				// explicit manifests to include.
				if info, err := os.Stat(path); err == nil {
					if info.Size() > 0 {
						systemManifests = append(systemManifests, path)
					}
				}
				path = filepath.Join(*packagesDir, packageName, "boot_manifest")
				if info, err := os.Stat(path); err == nil {
					if info.Size() > 0 {
						bootManifests = append(bootManifests, path)
					}
				}
			}
		} else {
			log.Printf("Packages file %q could not be opened (%s), ignoring", *packages, err)
		}
	}

	tempDir, err := ioutil.TempDir("", "make-fuchsia-vol")
	if err != nil {
		log.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	// persist the ramdisk in case a user wants it to boot qemu against a
	// persistent image
	ramdiskDir := fuchsiaBuildDir
	if ramdiskDir == "" {
		ramdiskDir = tempDir
	}
	ramdisk := filepath.Join(ramdiskDir, "bootdata.bin")

	args := []string{"-o", ramdisk, "--target=boot", *bootfsmanifest}
	cmd := exec.Command("mkbootfs", append(args, bootManifests...)...)
	b2, err := cmd.CombinedOutput()
	if err != nil {
		fmt.Printf("%s %v failed:\n%s\n", cmd.Path, cmd.Args, b2)
		log.Fatal(err)
	}

	log.Printf("combined bootdata written to: %s", ramdisk)

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
	if uint64(*efiSize+*sysSize+minGPTSize) > diskSize {
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

	g.Primary.Partitions = []gpt.PartitionEntry{}

	g.Update(logical, physical, optimal, diskSize) // for the firstusablelba

	efiStart, end := optimialBlockAlign(g.Primary.FirstUsableLBA, uint64(*efiSize), logical, physical, optimal)
	*efiSize = int64((end - efiStart) * logical)

	g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
		PartitionTypeGUID:   gpt.GUIDEFI,
		UniquePartitionGUID: gpt.NewRandomGUID(),
		PartitionName:       gpt.NewPartitionName("ESP"),
		StartingLBA:         efiStart,
		EndingLBA:           end,
	})

	if *rpi3 {
		var startingCHS [3]byte
		startingCHS[0] = byte(efiStart / (16 * 63))
		startingCHS[1] = byte((efiStart / 63) % 16)
		startingCHS[2] = byte((efiStart % 63) + 1)

		efiEnd := end
		var endingCHS [3]byte
		endingCHS[0] = byte(efiEnd / (16 * 63))
		endingCHS[1] = byte((efiEnd / 63) % 16)
		endingCHS[2] = byte((efiEnd % 63) + 1)

		// Install a "hybrid MBR" hack for the case of bios bootloaders that might
		// need it (e.g. rpi's binary blob that's stuck in MBR land).
		g.MBR.PartitionRecord[0] = mbr.PartitionRecord{
			BootIndicator: 0x80,
			StartingCHS:   startingCHS,
			EndingCHS:     endingCHS,
			OSType:        mbr.FAT32,
			StartingLBA:   uint32(efiStart),
			SizeInLBA:     uint32(uint64(*efiSize) / logical),
		}
	}

	var sysStart, blobStart, dataStart uint64

	if !*efiOnly {
		sysStart, end = optimialBlockAlign(end+1, uint64(*sysSize), logical, physical, optimal)
		*sysSize = int64((end - sysStart) * logical)

		g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaSystem,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("SYS"),
			StartingLBA:         sysStart,
			EndingLBA:           end,
		})

		blobStart, end = optimialBlockAlign(end+1, uint64(*blobSize), logical, physical, optimal)
		*blobSize = int64((end - blobStart) * logical)

		g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaBlob,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("BLOB"),
			StartingLBA:         blobStart,
			EndingLBA:           end,
		})

		dataStart, end = optimialBlockAlign(end+1, uint64(*dataSize), logical, physical, optimal)
		end = g.Primary.LastUsableLBA
		*dataSize = int64((end - dataStart) * logical)

		g.Primary.Partitions = append(g.Primary.Partitions, gpt.PartitionEntry{
			PartitionTypeGUID:   gpt.GUIDFuchsiaData,
			UniquePartitionGUID: gpt.NewRandomGUID(),
			PartitionName:       gpt.NewPartitionName("DATA"),
			StartingLBA:         dataStart,
			EndingLBA:           end,
		})
	}

	g.Update(logical, physical, optimal, diskSize)

	if err := g.Validate(); err != nil {
		log.Fatal(err)
	}

	log.Printf("EFI size: %d", *efiSize)
	if !*efiOnly {
		log.Printf("SYS size: %d", *sysSize)
		log.Printf("BLOB size: %d", *blobSize)
		log.Printf("DATA size: %d", *dataSize)
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

	efiStart = efiStart * logical
	if !*efiOnly {
		sysStart = sysStart * logical
		blobStart = blobStart * logical
		dataStart = dataStart * logical
	}

	log.Printf("Writing EFI partition and files")

	if _, err := exec.LookPath("mkfs-msdosfs"); err != nil {
		log.Fatal("Could not find mkfs-msdosfs, you might need to build, or change to $FUCHSIA_DIR")
	}

	cmd = exec.Command("mkfs-msdosfs", "-LESP", "-F32",
		fmt.Sprintf("-@%d", efiStart),
		fmt.Sprintf("-b%d", logical),
		fmt.Sprintf("-S%d", *efiSize),
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

	if !*rpi3 {
		msCopyIn(*bootloader, root, "EFI/BOOT")
	}
	msCopyIn(*kernel, root, "")
	msCopyIn(ramdisk, root, "")
	if _, err := os.Stat(*cmdline); err == nil {
		msCopyIn(*cmdline, root, "")
	}

	if *rpi3 {
		for _, f := range []string{"cmdline.txt", "config.txt", "bcm2710-rpi-3-b.dtb"} {
			msCopyIn(filepath.Join(*rpi3Root, f), root, "")
		}
		root.Rename(root, "zircon.bin", "kernel8.img")
		root.Sync()

		to, _, err := root.Open("start.elf", fs.OpenFlagWrite|fs.OpenFlagCreate|fs.OpenFlagFile)
		if err != nil {
			log.Fatal(err)
		}
		r, err := http.Get("https://github.com/raspberrypi/firmware/raw/390f53ed0fd79df274bdcc81d99e09fa262f03ab/boot/start.elf")
		if err != nil {
			log.Fatal(err)
		}
		b, err := ioutil.ReadAll(r.Body)
		if err != nil {
			log.Fatal(err)
		}
		if _, err := to.Write(b, 0, fs.WhenceFromCurrent); err != nil {
			log.Fatal(err)
		}
		r.Body.Close()
		to.Sync()
		to.Close()

		to, _, err = root.Open("bootcode.bin", fs.OpenFlagWrite|fs.OpenFlagCreate|fs.OpenFlagFile)
		if err != nil {
			log.Fatal(err)
		}
		r, err = http.Get("https://github.com/raspberrypi/firmware/raw/390f53ed0fd79df274bdcc81d99e09fa262f03ab/boot/bootcode.bin")
		if err != nil {
			log.Fatal(err)
		}
		b, err = ioutil.ReadAll(r.Body)
		if err != nil {
			log.Fatal(err)
		}
		if _, err := to.Write(b, 0, fs.WhenceFromCurrent); err != nil {
			log.Fatal(err)
		}
		r.Body.Close()
		to.Sync()
		to.Close()

		root.Sync()
	}

	root.Sync()
	root.Close()
	fatfs.Close()

	if !*efiOnly {
		log.Printf("Creating SYS minfs")
		path, err := mkminfs(*sysSize)
		if err != nil {
			log.Fatal(err)
		}
		defer os.Remove(path)

		dirs := map[string]struct{}{}
		minfsimg := fmt.Sprintf("%s@%d", path, *sysSize)

		for _, manifest := range systemManifests {
			m, err := os.Open(manifest)
			if err != nil {
				log.Fatal(err)
			}
			mb := bufio.NewReader(m)
			for {
				line, err := mb.ReadString('\n')
				if err == io.EOF {
					break
				}
				if err != nil {
					log.Print(err)
					break
				}
				parts := strings.SplitN(line, "=", 2)
				if len(parts) < 2 {
					continue
				}
				from := strings.TrimSpace(parts[1])
				to := "::" + strings.TrimSpace(parts[0])

				d := ""
				for _, part := range strings.Split(filepath.Dir(to), "/") {
					d = filepath.Join(d, part)
					if _, ok := dirs[d]; !ok {
						if err := minfs(minfsimg, "mkdir", d); err != nil {
							log.Fatal(err)
						}
						dirs[d] = struct{}{}
					}
				}
				if err := minfs(minfsimg, "cp", from, to); err != nil {
					log.Fatal(err)
				}
			}
			m.Close()
		}

		log.Print("Copying SYS into gpt image")
		if err := copyIn(disk, int64(sysStart), *sysSize, path); err != nil {
			log.Fatal(err)
		}
	}

	f.Sync()
	f.Close()

	log.Printf("Done")
}

func minfs(args ...string) error {
	cmd := exec.Command("minfs", args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	err := cmd.Run()
	if err != nil {
		return fmt.Errorf("minfs %s: %s", args, err)
	}
	return nil
}

func copyIn(dstpath string, offset, size int64, srcpath string) error {
	from, err := os.OpenFile(srcpath, os.O_RDONLY, 0)
	if err != nil {
		return err
	}
	defer from.Close()

	i, err := from.Stat()
	if err != nil {
		return err
	}

	if i.Size() > size {
		return fmt.Errorf("%s is larger than %d", srcpath, size)
	}

	to, err := os.OpenFile(dstpath, os.O_RDWR, 0)
	if err != nil {
		return err
	}
	defer to.Close()

	_, err = to.Seek(offset, io.SeekStart)
	if err != nil {
		return err
	}

	_, err = io.Copy(to, from)
	return err
}

func mkminfs(size int64) (string, error) {
	f, err := ioutil.TempFile("", "minfs")
	if err != nil {
		return "", err
	}
	// Truncate enough space for the tables, but not the whole volume
	f.Truncate(size)
	f.Close()
	path := f.Name()
	return path, minfs(fmt.Sprintf("%s@%d", path, size), "create")
}

// msCopyIn copies srcpath from the host filesystem into destdir under the given
// thinfs root.
func msCopyIn(srcpath string, root fs.Directory, destdir string) {
	d := root
	defer d.Sync()

	dStack := []fs.Directory{}

	defer func() {
		for _, d := range dStack {
			d.Sync()
			d.Close()
		}
	}()

	if destdir != "" && destdir != "/" {
		for _, part := range strings.Split(destdir, "/") {
			var err error
			_, d, err = d.Open(part, fs.OpenFlagRead|fs.OpenFlagWrite|fs.OpenFlagCreate|fs.OpenFlagDirectory)
			if err != nil {
				log.Fatalf("open/create %s: %#v %s", part, err, err)
			}
			d.Sync()
			dStack = append(dStack, d)
		}
	}

	name := filepath.Base(srcpath)

	to, _, err := d.Open(name, fs.OpenFlagWrite|fs.OpenFlagCreate|fs.OpenFlagFile)
	if err != nil {
		log.Fatalf("creating %s in msdosfs: %s", name, err)
	}
	defer to.Close()

	from, err := os.Open(srcpath)
	if err != nil {
		log.Fatal(err)
	}
	defer from.Close()

	b := make([]byte, 4096)
	for err == nil {
		var n int
		n, err = from.Read(b)
		if n > 0 {
			if _, err := to.Write(b, 0, fs.WhenceFromCurrent); err != nil {
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
