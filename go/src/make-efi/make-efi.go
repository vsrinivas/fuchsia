// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"

	"thinfs/block/file"
	"thinfs/fs"
	"thinfs/fs/msdosfs"
)

var (
	mkfs          = flag.String("mkfs", "", "Path to mkfs-msdosfs host tool from Zircon")
	target        = flag.String("output", "", "Target file/disk to write EFI partition to")
	size          = flag.Uint64("size", 0, "(optional) Size of partition to create")
	offset        = flag.Uint64("offset", 0, "(optional) Offset into target to write partition to")
	zircon        = flag.String("zircon", "", "(optional) Path to source file for zircon.bin")
	bootdata      = flag.String("bootdata", "", "(optional) Path to source file for bootdata.bin")
	efiBootloader = flag.String("efi-bootloader", "", "(optional) Path to source file for EFI/BOOT/BOOTX64.EFI")
	manifest      = flag.String("manifest", "", "(optional) Path to a manifest file of the form `dst=src\n` to import to partition")
	zedboot       = flag.String("zedboot", "", "(optional) Path to a source file for zedboot.bin")
	cmdline       = flag.String("cmdline", "", "(optional) Bootloader cmdline file")
)

// minSize is the minimum image size, as the tool currently always builds fat32.
const minSize = 63 * 1024 * 1024

func main() {
	flag.Parse()

	if *target == "" || *mkfs == "" {
		flag.CommandLine.Usage()
		fmt.Printf("\nerror: -target and -mkfs are required\n")
		os.Exit(1)
	}

	var err error
	*mkfs, err = filepath.Abs(*mkfs)
	if err != nil {
		log.Fatal(err)
	}
	*target, err = filepath.Abs(*target)
	if err != nil {
		log.Fatal(err)
	}

	// Slurp up all the copies to do, as it will make the final copy code simpler.
	// It can also be used to compute the size if the size was not given.
	dstSrc := map[string]string{}
	if *zircon != "" {
		dstSrc["zircon.bin"] = *zircon
	}
	if *bootdata != "" {
		dstSrc["bootdata.bin"] = *bootdata
	}
	if *efiBootloader != "" {
		dstSrc["EFI/BOOT/BOOTX64.EFI"] = *efiBootloader

		tf, err := ioutil.TempFile("", "gsetup-boot")
		if err != nil {
			log.Fatal(err)
		}
		tf.WriteString("efi\\boot\\bootx64.efi")
		tf.Close()
		defer os.Remove(tf.Name())
		dstSrc["EFI/Google/GSetup/Boot"] = tf.Name()
	}
	if *zedboot != "" {
		dstSrc["zedboot.bin"] = *zedboot
	}
	if *cmdline != "" {
		dstSrc["cmdline"] = *cmdline
	}
	if *manifest != "" {
		r, err := newManifestReader(*manifest)
		if err != nil {
			log.Fatal(err)
		}
		for {
			dst, src, err := r.Next()
			if dst != "" && src != "" {
				dstSrc[dst] = src
			}
			if err == io.EOF {
				break
			}
			if err != nil {
				log.Fatal(err)
			}
		}
		r.Close()
	}

	if *size == 0 {
		*size = computeSize(dstSrc)
	}
	*size = alignSize(*size)

	f, err := os.Create(*target)
	if err != nil {
		log.Fatal(err)
	}
	f.Close()

	args := []string{*mkfs,
		"-@", strconv.FormatUint(*offset, 10),
		"-S", strconv.FormatUint(*size, 10),
		"-F", "32",
		"-L", "ESP",
		"-O", "Fuchsia",
		"-b", "512",
		*target}
	cmd := exec.Command(args[0], args[1:]...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		os.Stderr.Write(output)
		log.Fatalf("mkfs did not succceed, see output above.\n%s\n", strings.Join(args, " "))
	}

	f, err = os.OpenFile(*target, os.O_RDWR|os.O_CREATE, 0644)
	if err != nil {
		log.Fatal(err)
	}
	dev, err := file.NewRange(f, 512, int64(*offset), int64(*size))
	if err != nil {
		log.Fatal(err)
	}

	fatfs, err := msdosfs.New(*target, dev, fs.ReadWrite|fs.Force)
	if err != nil {
		log.Fatal(err)
	}

	root := fatfs.RootDirectory()
	for dst, src := range dstSrc {
		msCopyIn(root, src, dst)
	}

	root.Sync()
	if err := root.Close(); err != nil {
		log.Fatal(err)
	}
	if err := fatfs.Close(); err != nil {
		log.Fatal(err)
	}
	f.Sync()
	if err := f.Close(); err != nil {
		log.Fatal(err)
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

func getSize(f string) uint64 {
	if f == "" {
		return 0
	}
	fi, err := os.Stat(f)
	if err != nil {
		log.Fatal(err)
	}
	return uint64(fi.Size())
}

type manifestReader struct {
	*os.File
	*bufio.Reader
	manifestRoot string
}

func newManifestReader(path string) (*manifestReader, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	return &manifestReader{
		f,
		bufio.NewReader(f),
		filepath.Dir(path),
	}, nil
}

func (r *manifestReader) Next() (dst, src string, err error) {
	for err == nil {
		line, err := r.ReadString('\n')
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "#") {
			continue
		}
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			log.Printf("make-efi: bad manifest line format: %q, skipping", line)
			continue
		}
		return parts[0], filepath.Join(r.manifestRoot, parts[1]), err
	}
	return "", "", err
}

func computeSize(dstSrc map[string]string) uint64 {
	var total uint64
	for _, src := range dstSrc {
		total += getSize(src)
	}
	pad := total % 63
	if pad != 0 {
		total += pad
	}
	if total < minSize {
		return minSize
	}

	return total
}

// sectors per track is 63, and a sector is 512, so we must round to the nearest
// 32256.
const sizeAlignment = 32256

func alignSize(total uint64) uint64 {
	if d := total % sizeAlignment; d != 0 {
		total = total + (sizeAlignment - d)
	}
	return total
}
