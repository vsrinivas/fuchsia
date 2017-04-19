// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"fuchsia.googlesource.com/thinfs/gpt"
	"fuchsia.googlesource.com/thinfs/mbr"
)

type part struct {
	// label is the target partition label
	label string
	// guid is the partition type GUID
	guid gpt.GUID
	// size is a number of bytes if positive, or a percentage if negative
	size int64
}

type plan struct {
	disk       string
	partitions []part
}

func parseArgs(args []string) (plan, error) {
	plan := plan{}

	if len(args) < 1 {
		return plan, fmt.Errorf("no disk supplied")
	}
	plan.disk = args[0]

	args = args[1:]

	if len(args)%3 != 0 {
		return plan, fmt.Errorf("wrong number of arguments")
	}

	for i := 0; i < len(args); i += 3 {
		label, guid, size := args[i], args[i+1], args[i+2]

		if err := checkLabel(label); err != nil {
			return plan, err
		}
		g, err := parseGUID(guid)
		if err != nil {
			return plan, err
		}
		sz, err := parseSize(size)
		if err != nil {
			return plan, err
		}

		plan.partitions = append(plan.partitions, part{label, g, sz})
	}

	return plan, nil
}

func (p plan) apply() error {
	f, err := os.OpenFile(p.disk, os.O_RDWR, 0640)
	if err != nil {
		return err
	}

	logical, err := gpt.GetLogicalBlockSize(f)
	if err != nil {
		fmt.Fprintf(os.Stderr, "WARNING: fallback logocal block size: %d\n", logical)
	}
	physical, err := gpt.GetPhysicalBlockSize(f)
	if err != nil {
		fmt.Fprintf(os.Stderr, "WARNING: fallback physical block size: %d\n", physical)
	}
	optimal, err := gpt.GetOptimalTransferSize(f)
	if err != nil {
		fmt.Fprintf(os.Stderr, "WARNING: fallback optimal block size: %d\n", optimal)
	}

	diskSize, err := gpt.GetDiskSize(f)
	if err != nil {
		return err
	}

	free := diskSize
	portions := map[int]uint64{}
	for i, p := range p.partitions {
		// proportional partition sizes are stored with negative values
		if p.size > 0 {
			free -= uint64(p.size)
		} else {
			portions[i] = uint64(-p.size)
		}
	}

	if free < 0 {
		return fmt.Errorf("insufficient space for given partition plan")
	}

	var t uint64
	for _, p := range portions {
		t += p
	}
	if t > 100 {
		return fmt.Errorf("proportional sizes total > 100%%: %d", t)
	}

	for i, por := range portions {
		p.partitions[i].size = int64((float64(free) / 100) * float64(por))
	}

	g, err := gpt.ReadGPT(f, logical, diskSize)
	if err != nil {
		return err
	}

	g.Primary = gpt.PartitionTable{}
	g.Backup = gpt.PartitionTable{}

	// always preserve mbr fields that efi/gpt doesn't care about
	m := g.MBR

	g.MBR = mbr.NewProtectiveMBR(upDiv(diskSize, logical))
	g.MBR.BootCode = m.BootCode
	g.MBR.Pad = m.Pad
	g.MBR.UniqueMBRDiskSignature = m.UniqueMBRDiskSignature
	g.MBR.Unknown = m.Unknown

	// update to get a valid g.Primary.FirstUsableLBA
	if err := g.Update(logical, physical, optimal, diskSize); err != nil {
		return err
	}

	start, end := g.Primary.FirstUsableLBA, uint64(0)
	for _, p := range p.partitions {
		start, end = gpt.AlignedRange(start, uint64(p.size), logical, physical, optimal)
		pe := gpt.PartitionEntry{
			PartitionTypeGUID: p.guid,
			StartingLBA:       start,
			EndingLBA:         end,
			PartitionName:     gpt.NewPartitionName(p.label),
		}
		g.Primary.Partitions = append(g.Primary.Partitions, pe)
		start = end + 1
	}

	if err := g.Update(logical, physical, optimal, diskSize); err != nil {
		return err
	}

	if _, err := f.Seek(0, io.SeekStart); err != nil {
		return err
	}

	_, err = g.WriteTo(f)
	return err
}

func upDiv(a, b uint64) uint64 {
	return uint64(math.Ceil(float64(a) / float64(b)))
}

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `Usage %s: disk [partitions]
    partitions: LABEL GUID/NAME SIZE

    LABEL:       any string < 36 characters
    GUID/NAME:  GUID in hex, or a named partition type e.g. EFI
    SIZE:       number and unit (k, m, g), or a percentage (of free space)

    e.g.  EFI ESP 100m \
          SYS FuchsiaSystem 5g \
          DATA FuchsiaData 50%% \
          BLOB FuchsiaBlob 50%%\n`,
		filepath.Base(os.Args[0]))
		flag.PrintDefaults()
	}

	flag.Parse()

	plan, err := parseArgs(flag.Args())
	if err != nil {
		fatalerr(err)
	}

	plan.apply()
}

func fatalerr(err error) {
	fmt.Fprintf(os.Stderr, "fatal: %s", err)
	os.Exit(1)
}

func checkLabel(s string) error {
	if s == "" {
		return fmt.Errorf("empty label")
	}

	return nil
}

func parseGUID(s string) (gpt.GUID, error) {
	if s == "" {
		return gpt.GUID{}, fmt.Errorf("empty guid")
	}

	if g, ok := gpt.GUIDS[strings.ToLower(s)]; ok {
		return g, nil
	}
	g, err := gpt.NewGUID(s)
	if err != nil {
		err = fmt.Errorf("unknown guid %q", s)
	}

	return g, err
}

func parseSize(s string) (int64, error) {
	if s == "" {
		return 0, fmt.Errorf("empty size")
	}

	// trim the string to numbers, and track what is removed. if what is removed
	// is not an acceptable unit suffix then error.
	var unit string
	nums := strings.TrimFunc(s, func(r rune) bool {
		if r >= '0' && r <= '9' || r == '-' || r == '.' {
			return false
		}
		unit += string(r)
		return true
	})

	n, err := strconv.ParseInt(nums, 10, 64)
	if err != nil {
		err = fmt.Errorf("can't parse size %q", s)
	}

	switch strings.ToLower(unit) {
	case "g", "gb":
		n *= 1024 * 1024 * 1024
	case "m", "mb":
		n *= 1024 * 1024
	case "k", "kb":
		n *= 1024
	case "%":
		n = -n
	case "":
	default:
		err = fmt.Errorf("unknown unit %q", unit)
	}

	return n, err
}
