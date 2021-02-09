// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// make-fuchsia-vol is a temporarily useful script that provisions Fuchsia
// volumes based on paths provided.

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/src/lib/thinfs/fs"
)

var (
	verbose         = flag.Bool("verbose", false, "enable verbose logging")
	fuchsiaBuildDir = flag.String("fuchsia-build-dir", os.Getenv("FUCHSIA_BUILD_DIR"), "fuchsia build dir")

	bootloader = flag.String("bootloader", "", "path to bootx64.efi")
	zbi        = flag.String("zbi", "", "path to zbi (default: zircon-a from image manifests)")
	cmdline    = flag.String("cmdline", "", "path to command line file (if exists)")
	zedboot    = flag.String("zedboot", "", "path to zedboot.zbi (default: zircon-r from image manifests)")

	ramdiskOnly  = flag.Bool("ramdisk-only", false, "ramdisk-only mode - only write an ESP partition")
	blob         = flag.String("blob", "", "path to blob partition image (not used with ramdisk)")
	data         = flag.String("data", "", "path to data partition image (not used with ramdisk)")
	useSparseFvm = flag.Bool("use-sparse-fvm", false, "if true, use sparse fvm instead of full fvm")
	sparseFvm    = flag.String("sparse-fvm", "", "path to sparse FVM image (default: storage-sparse from image manifests)")

	abr        = flag.Bool("abr", true, "add Zircon-{A,B,R} partitions")
	zirconA    = flag.String("zirconA", "", "path to partition image for Zircon-A (default: from -zbi)")
	vbmetaA    = flag.String("vbmetaA", "", "path to partition image for Vbmeta-A")
	zirconB    = flag.String("zirconB", "", "path to partition image for Zircon-B (default: from -zbi)")
	vbmetaB    = flag.String("vbmetaB", "", "path to partition image for Vbmeta-B")
	zirconR    = flag.String("zirconR", "", "path to partition image for Zircon-R (default: zircon-r from image manifests)")
	vbmetaR    = flag.String("vbmetaR", "", "path to partition image for Vbmeta-R")
	abrSize    = flag.Int64("abr-size", 256*1024*1024, "Kernel partition size for A/B/R")
	vbmetaSize = flag.Int64("vbmeta-size", 8*1024, "partition size for vbmeta A/B/R")
	abrBoot    = flag.String("abr-boot", "a", "A/B/R partition to boot by default")

	blockSize           = flag.Int64("block-size", 0, "the block size of the target disk (0 means detect)")
	physicalBlockSize   = flag.Int64("physical-block-size", 0, "the physical block size of the target disk (0 means detect)")
	optimalTransferSize = flag.Int64("optimal-transfer-size", 0, "the optimal transfer size of the target disk (0 means unknown/unused)")

	efiSize = flag.Int64("efi-size", 63*1024*1024, "efi partition size in bytes")
	fvmSize = flag.Int64("fvm-size", 0, "fvm partition size in bytes (0 means `fill`)")
	resize  = flag.Int64("resize", 0, "create or resize the image to this size in bytes")
)

// imageManifest is basename of the image manifest file.
const imageManifest = "images.json"

type imageManifestEntry struct {
	Name string `json:"name"`
	Path string `json:"path"`
	Type string `json:"type"`
}

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
	if *fuchsiaBuildDir == "" {
		return
	}
	f, err := os.Open(filepath.Join(*fuchsiaBuildDir, imageManifest))
	if err != nil {
		log.Printf("warning: failed to load %s: %v", imageManifest, err)
		return
	}
	defer f.Close()

	var entries []imageManifestEntry
	if err := json.NewDecoder(f).Decode(&entries); err != nil {
		log.Printf("warning: failed to load %s: %v", imageManifest, err)
		return
	}

	for _, image := range entries {
		imagePaths[image.Type+"_"+image.Name] = image.Path
	}
}

func needFuchsiaBuildDir() {
	if *fuchsiaBuildDir == "" {
		log.Fatalf("either pass -fuchsia-build-dir or set $FUCHSIA_BUILD_DIR")
	}
}

func main() {
	bootMode := initArguments()
	disk, err := filepath.Abs(flag.Args()[0])
	if err != nil {
		log.Fatal(err)
	}
	diskInfo := prepareDisk(disk)

	sizes := partitionSizes{
		efiSize:    uint64(*efiSize),
		abrSize:    uint64(*abrSize),
		vbmetaSize: uint64(*vbmetaSize),
		fvmSize:    uint64(*fvmSize),
	}
	partitions := createPartitionTable(&diskInfo, &sizes, *abr, *verbose, *ramdiskOnly, *useSparseFvm)
	writeDisk(disk, partitions, diskInfo, sizes, bootMode)
}

func initArguments() BootPartition {
	flag.Parse()
	tryLoadManifests()

	if *bootloader == "" {
		needFuchsiaBuildDir()
		*bootloader = filepath.Join(*fuchsiaBuildDir, "efi_x64/bootx64.efi")
	}
	if _, err := os.Stat(*bootloader); err != nil {
		log.Fatalf("cannot read %q: %s", *bootloader, err)
	}

	if *zbi == "" {
		needFuchsiaBuildDir()
		*zbi = filepath.Join(*fuchsiaBuildDir, getImage("zbi_zircon-a"))
	}
	if _, err := os.Stat(*zbi); err != nil {
		log.Fatalf("cannot read %q: %s", *zbi, err)
	}

	if *zedboot == "" {
		needFuchsiaBuildDir()
		*zedboot = filepath.Join(*fuchsiaBuildDir, getImage("zbi_zircon-r"))
	}
	if _, err := os.Stat(*zedboot); err != nil {
		log.Fatalf("cannot read %q: %s", *zedboot, err)
	}

	if *cmdline == "" {
		needFuchsiaBuildDir()
		p := filepath.Join(*fuchsiaBuildDir, "cmdline")
		if _, err := os.Stat(p); err == nil {
			*cmdline = p
		}
	} else {
		if _, err := os.Stat(*cmdline); err != nil {
			log.Fatal(err)
		}
	}

	bootMode := BOOT_A
	if *abr {
		if *zirconA == "" {
			needFuchsiaBuildDir()
			*zirconA = *zbi
		}
		if *vbmetaA == "" {
			*vbmetaA = filepath.Join(*fuchsiaBuildDir, getImage("vbmeta_zircon-a"))
		}
		if *zirconB == "" {
			*zirconB = *zbi
		}
		if *vbmetaB == "" {
			*vbmetaB = filepath.Join(*fuchsiaBuildDir, getImage("vbmeta_zircon-a"))
		}
		if *zirconR == "" {
			*zirconR = filepath.Join(*fuchsiaBuildDir, getImage("zbi_zircon-r"))
		}
		if *vbmetaR == "" {
			*vbmetaR = filepath.Join(*fuchsiaBuildDir, getImage("vbmeta_zircon-r"))
		}

		switch strings.ToLower(*abrBoot) {
		case "a":
			bootMode = BOOT_A
		case "b":
			bootMode = BOOT_B
		case "r":
			bootMode = BOOT_RECOVERY
		default:
			log.Fatalf("Invalid -abr-boot passed: expected 'a', 'b', or 'r'.")
		}
	}

	if *useSparseFvm {
		if *sparseFvm == "" {
			needFuchsiaBuildDir()
			*sparseFvm = filepath.Join(*fuchsiaBuildDir, getImage("blk_storage-sparse"))
		}
		stat, err := os.Stat(*sparseFvm)
		if err != nil {
			log.Fatalf("sparse fvm error: %s\n", err)
		}
		*fvmSize = stat.Size()
	}

	if !*ramdiskOnly && !*useSparseFvm {
		if *blob == "" {
			needFuchsiaBuildDir()
			*blob = filepath.Join(*fuchsiaBuildDir, getImage("blk_blob"))
		}
		if *data == "" {
			needFuchsiaBuildDir()
			*data = filepath.Join(*fuchsiaBuildDir, getImage("blk_data"))
		}

		if _, err := os.Stat(*blob); err != nil {
			log.Fatalf("Blob image error: %s\nEither provide a blob image, or pass -ramdisk-only", err)
		}
		if _, err := os.Stat(*data); err != nil {
			log.Fatalf("Data image error: %s\nEither provide a data image, or pass -ramdisk-only", err)
		}
	}

	if len(flag.Args()) != 1 {
		flag.Usage()
		os.Exit(1)
	}

	return bootMode
}

func partitionCopy(f *os.File, start, size int64, path string) {
	input, err := os.Open(path)
	if err != nil {
		log.Fatalf("partition copy failed for input: %s: %s", path, err)
	}
	defer input.Close()
	input_info, err := input.Stat()
	if err != nil {
		log.Fatalf("stat failed for input: %s: %s", path, err)
	}
	if input_info.Size() > size {
		log.Printf("WARNING: %s is larger than the provided ABR size", path)
	}
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
	cmd := exec.Command(fuchsiaTool("fvm"), argv...)
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
// start block address and the address of the last block of the partition.
func optimalBlockAlign(first, byteSize, logical, physical, optimal uint64) (start, end uint64) {
	var alignTo = logical
	if physical > alignTo {
		alignTo = physical
	}
	if optimal > alignTo {
		alignTo = optimal
	}

	lAlign := alignTo / logical

	start = first
	if d := first % lAlign; d != 0 {
		start = first + lAlign - d
	}

	lSize := byteSize / logical
	if byteSize%logical != 0 {
		lSize++
	}

	// "start" is inclusive, so to get lSize blocks total, we need the first block + lSize - 1 extra blocks.
	end = start + lSize - 1
	return
}

func fuchsiaTool(name string) string {
	var tool string
	tool, _ = exec.LookPath(name)
	if tool == "" {
		needFuchsiaBuildDir()
		tool, _ = exec.LookPath(filepath.Join(*fuchsiaBuildDir, "host_x64", name))
	}
	if tool == "" {
		log.Fatalf("Could not find %q, you might need to build fuchsia", name)
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
