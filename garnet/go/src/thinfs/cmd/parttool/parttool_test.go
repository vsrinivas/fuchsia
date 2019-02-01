// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"testing"

	"thinfs/gpt"
)

var parseArgsExamples = []struct {
	args []string
	plan plan
	err  error
}{
	{[]string{}, plan{}, fmt.Errorf("no disk supplied")},
	{[]string{"/tmp/dsk"}, plan{disk: "/tmp/dsk"}, nil},
	{
		[]string{"/tmp/dsk", ""},
		plan{disk: "/tmp/dsk"},
		fmt.Errorf("wrong number of arguments"),
	},
	{
		[]string{"/tmp/dsk", "", ""},
		plan{disk: "/tmp/dsk"},
		fmt.Errorf("wrong number of arguments"),
	},
	{
		[]string{"/tmp/dsk", "", "", "", ""},
		plan{disk: "/tmp/dsk"},
		fmt.Errorf("wrong number of arguments"),
	},
	{
		[]string{"/tmp/dsk", "", "", ""},
		plan{disk: "/tmp/dsk"},
		fmt.Errorf("empty label"),
	},
	{
		[]string{"/tmp/dsk", "ESP", "", ""},
		plan{disk: "/tmp/dsk"},
		fmt.Errorf("empty guid"),
	},
	{
		[]string{"/tmp/dsk", "ESP", "EFI", ""},
		plan{disk: "/tmp/dsk"},
		fmt.Errorf("empty size"),
	},
	{
		[]string{"/tmp/dsk", "ESP", "derp", ""},
		plan{disk: "/tmp/dsk"},
		fmt.Errorf("unknown guid \"derp\""),
	},
	{
		[]string{"/tmp/dsk", "ESP", "EFI", "size"},
		plan{disk: "/tmp/dsk"},
		fmt.Errorf("unknown unit \"size\""),
	},
	{
		[]string{"/tmp/dsk", "ESP", "EFI", "10g"},
		plan{
			disk:       "/tmp/dsk",
			partitions: []part{part{label: "ESP", guid: gpt.GUIDEFI, size: 10737418240}},
		},
		nil,
	},
	{
		[]string{"/tmp/dsk", "ESP", "EFI", "10m"},
		plan{
			disk:       "/tmp/dsk",
			partitions: []part{part{label: "ESP", guid: gpt.GUIDEFI, size: 10485760}},
		},
		nil,
	},
	{
		[]string{"/tmp/dsk", "ESP", "EFI", "10k"},
		plan{
			disk:       "/tmp/dsk",
			partitions: []part{part{label: "ESP", guid: gpt.GUIDEFI, size: 10240}},
		},
		nil,
	},

	{
		[]string{"/tmp/dsk",
			"ESP", "EFI", "100m",
			"SYS", "fuchsia-system", "5g",
			"DATA", "fuchsia-data", "50%",
			"BLOB", "fuchsia-blob", "50%",
		},
		plan{
			disk: "/tmp/dsk",
			partitions: []part{
				part{label: "ESP", guid: gpt.GUIDEFI, size: 104857600},
				part{label: "SYS", guid: gpt.GUIDFuchsiaSystem, size: 5368709120},
				part{label: "DATA", guid: gpt.GUIDFuchsiaData, size: -50},
				part{label: "BLOB", guid: gpt.GUIDFuchsiaBlob, size: -50},
			},
		},
		nil,
	},
}

func TestParseArgs(t *testing.T) {
	for _, ex := range parseArgsExamples {
		t.Run(fmt.Sprintf("parseArgs(%v)", ex.args), func(t *testing.T) {
			plan, err := parseArgs(ex.args)
			if !(err == nil && ex.err == nil) {
				exs, errs := "no error", "no error"
				if ex.err != nil {
					exs = ex.err.Error()
				}
				if err != nil {
					errs = err.Error()
				}
				if errs != exs {
					t.Errorf("got %q, want %q", errs, exs)
					return
				}
			}

			if plan.disk != ex.plan.disk {
				t.Errorf("got %s, want %s", plan.disk, ex.plan.disk)
			}

			if len(plan.partitions) != len(ex.plan.partitions) {
				t.Errorf("got %d, want %d", len(plan.partitions), len(ex.plan.partitions))
				t.Errorf("got %#v, want %#v", plan.partitions, ex.plan.partitions)
				return
			}

			for i, part := range plan.partitions {
				epart := ex.plan.partitions[i]

				if part.label != epart.label {
					t.Errorf("got %s, want %s", part.label, epart.label)
				}
				if part.guid != epart.guid {
					t.Errorf("got %s, want %s", part.guid, epart.guid)
				}
				if part.size != epart.size {
					t.Errorf("got %d, want %d", part.size, epart.size)
				}
			}
		})
	}
}

func TestPlanApply(t *testing.T) {
	f, err := ioutil.TempFile("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(f.Name())
	defer f.Close()
	var size int64 = 10 * 1024 * 1024 * 1024
	f.Truncate(size)
	f.Sync()

	p := plan{
		disk: f.Name(),
		partitions: []part{
			part{label: "ESP", guid: gpt.GUIDEFI, size: 104857600},
			part{label: "SYS", guid: gpt.GUIDFuchsiaSystem, size: 5368709120},
			part{label: "DATA", guid: gpt.GUIDFuchsiaData, size: -50},
			part{label: "BLOB", guid: gpt.GUIDFuchsiaBlob, size: -50},
		},
	}

	if err := p.apply(); err != nil {
		t.Fatal(err)
	}

	if _, err := f.Seek(0, io.SeekStart); err != nil {
		t.Fatal(err)
	}
	g, err := gpt.ReadGPT(f, 512, uint64(size))
	if err != nil {
		t.Fatal(err)
	}

	free := uint64(size)
	portions := map[int]int64{}
	ptotals := int64(100)
	for i, p := range p.partitions {
		if p.size > 0 {
			free -= uint64(p.size)
		} else {
			portions[i] = -p.size
			ptotals += p.size
			if ptotals < 0 {
				t.Fatalf("partition table requested more than 100%% division of free space")
			}
		}
	}
	for i, portion := range portions {
		p.partitions[i].size = int64((float64(free) / 100) * float64(portion))
	}

	logical, _ := gpt.GetLogicalBlockSize(f)
	physical, _ := gpt.GetPhysicalBlockSize(f)
	optimal, _ := gpt.GetOptimalTransferSize(f)

	maxPad := logical
	if physical > maxPad {
		maxPad = physical
	}
	if optimal > maxPad {
		maxPad = optimal
	}

	for i, p := range p.partitions {
		gp := g.Primary.Partitions[i]
		if got, want := gp.PartitionName.String(), p.label; got != want {
			t.Errorf("part[%d] label: got %q, want %q", i, got, want)
		}
		if got, want := gp.PartitionTypeGUID, p.guid; got != want {
			t.Errorf("part[%d] guid: got %x, want %x", i, got, want)
		}
		size := (gp.EndingLBA - gp.StartingLBA) * 512
		if size-uint64(p.size) < 0 {
			t.Errorf("part[%d] too small: got %d, want %d", i, size, p.size)
		}
		if size-uint64(p.size) > maxPad {
			t.Errorf("part[%d] too much padding: got %d, want %d", i, size, p.size)
		}
	}
}
