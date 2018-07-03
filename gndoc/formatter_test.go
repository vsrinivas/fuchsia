// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gndoc

import (
	"bytes"
	"testing"
)

const (
	numKeys = 2
)

var (
	defaultx64Arg = Arg{
		Name: "default",
		DefaultVal: argValue{
			Val:  "false",
			File: "//test/BUILD.gn",
			Line: 2,
		},
		Comment: "Description of default arg.\n",
		Key:     "target_cpu = x64",
	}

	defaultarm64Arg = Arg{
		Name: "default",
		DefaultVal: argValue{
			Val:  "false",
			File: "//test/BUILD.gn",
			Line: 2,
		},
		Comment: "Description of default arg.\n",
		Key:     "target_cpu = arm64",
	}

	defaultarm64ArgWithCurrent = Arg{
		Name: "default_current",
		CurrentVal: argValue{
			Val:  "[1, 2]",
			File: "//build/BUILD.gn",
			Line: 24,
		},
		DefaultVal: argValue{
			Val:  "[3, 4]",
			File: "//base/BUILD.gn",
			Line: 4,
		},
		Comment: "Description of default_current arg.\n",
		Key:     "target_cpu = arm64",
	}

	defaultx64ArgWithCurrent = Arg{
		Name: "default_current",
		CurrentVal: argValue{
			Val:  "3",
			File: "//build/BUILD.gn",
			Line: 24,
		},
		DefaultVal: argValue{
			Val:  "4",
			File: "//base/BUILD.gn",
			Line: 2,
		},
		Comment: "Description of default_current arg.\n",
		Key:     "target_cpu = x64",
	}

	x64Arg = Arg{
		Name: "x64",
		CurrentVal: argValue{
			Val: "1",
		},
		DefaultVal: argValue{
			Val: "2",
		},
		Comment: "Description of x64 arg that references //build/path.py, //sources, and //base.\n",
		Key:     "target_cpu = x64",
	}

	arm64Arg = Arg{
		Name: "arm64",
		CurrentVal: argValue{
			Val: "arg",
		},
		DefaultVal: argValue{
			Val: "value",
		},
		Comment: "Description of arm64 arg.\n",
		Key:     "target_cpu = arm64",
	}

	twoKeyarm64TestArg = Arg{
		Name: "arm64",
		CurrentVal: argValue{
			Val: "arg",
		},
		DefaultVal: argValue{
			Val: "value",
		},
		Comment: "Description of arm64 arg.\n",
		Key:     "target_cpu = arm64, package='test/package/default'",
	}

	twoKeyx64TestArg = Arg{
		Name: "x64",
		CurrentVal: argValue{
			Val: "arg",
		},
		DefaultVal: argValue{
			Val: "value",
		},
		Comment: "Description of x64 arg.\n",
		Key:     "target_cpu = x64, package='test/package/default'",
	}

	twoKeyarm64OtherArg = Arg{
		Name: "arm64Other",
		CurrentVal: argValue{
			Val: "arg",
		},
		DefaultVal: argValue{
			Val: "value",
		},
		Comment: "Description of arm64 arg.\n",
		Key:     "target_cpu = arm64, package='other/package/default'",
	}

	twoKeyx64OtherArg = Arg{
		Name: "x64Other",
		CurrentVal: argValue{
			Val: "arg",
		},
		DefaultVal: argValue{
			Val: "value",
		},
		Comment: "Description of x64 arg.\n",
		Key:     "target_cpu = x64, package='other/package/default'",
	}

	newLineValueArg = Arg{
		Name: "NewLine",
		DefaultVal: argValue{
			Val: "{\n  base = \"//build/toolchain/fuchsia:x64\"\n}",
		},
		Comment: "Description of newline arg.\n",
		Key:     "target_cpu = x64, package='other/package/default'",
	}
)

func Sources() *SourceMap {
	s := SourceMap(make(map[string]string))
	s["base"] = "http://fuchsia.com/base"
	s["build"] = "http://fuchsia.com/build"
	return &s
}

func TestDefault(t *testing.T) {

	gnArgs := []Arg{defaultx64Arg, defaultarm64Arg}
	argMap := NewArgMap(Sources())
	for _, arg := range gnArgs {
		argMap.AddArg(arg)
	}

	// No file name emits to stdout.
	var buffer bytes.Buffer
	argMap.EmitMarkdown(&buffer)

	actual := buffer.String()
	expected := `# GN Build Arguments

## All builds

### default
Description of default arg.

**Current value (from the default):** ` + "`false`" + `

From //test/BUILD.gn:2

`
	if expected != actual {
		t.Fatalf("In TestDefault, expected \n%s but got \n%s", expected, actual)
	}
}

func TestDefaultWithCurrent(t *testing.T) {

	gnArgs := []Arg{defaultx64ArgWithCurrent, defaultarm64ArgWithCurrent}
	argMap := NewArgMap(Sources())
	for _, arg := range gnArgs {
		argMap.AddArg(arg)
	}

	// No file name emits to stdout.
	var buffer bytes.Buffer
	argMap.EmitMarkdown(&buffer)

	actual := buffer.String()
	expected := `# GN Build Arguments

## All builds

### default_current
Description of default_current arg.

**Current value for ` + "`target_cpu = arm64`:** `[1, 2]`" + `

From [//build/BUILD.gn:24](http://fuchsia.com/build/BUILD.gn#24)

**Overridden from the default:** ` + "`[3, 4]`" + `

From [//base/BUILD.gn:4](http://fuchsia.com/base/BUILD.gn#4)

**Current value for ` + "`target_cpu = x64`:** `3`" + `

From [//build/BUILD.gn:24](http://fuchsia.com/build/BUILD.gn#24)

**Overridden from the default:**` + " `4`" + `

From [//base/BUILD.gn:2](http://fuchsia.com/base/BUILD.gn#2)

`

	if expected != actual {
		t.Fatalf("In TestDefaultWithCurrent, expected \n%s but got \n%s", expected, actual)
	}
}

func TestUnique(t *testing.T) {

	gnArgs := []Arg{x64Arg, arm64Arg}
	argMap := NewArgMap(Sources())
	for _, arg := range gnArgs {
		argMap.AddArg(arg)
	}

	// No file name emits to stdout.
	var buffer bytes.Buffer
	argMap.EmitMarkdown(&buffer)

	actual := buffer.String()
	expected := `# GN Build Arguments

## ` + "`target_cpu = arm64`" + `

### arm64
Description of arm64 arg.

**Current value for ` + "`target_cpu = arm64`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

## ` + "`target_cpu = x64`" + `

### x64
Description of x64 arg that references [//build/path.py](http://fuchsia.com/build/path.py), //sources, and [//base](http://fuchsia.com/base).

**Current value for ` + "`target_cpu = x64`:** `1`" + `

**Overridden from the default:** ` + "`2`" + `

`
	if expected != actual {
		t.Fatalf("In TestUnique, expected \n%s but got \n%s", expected, actual)
	}
}

func TestTwoKeys(t *testing.T) {

	gnArgs := []Arg{twoKeyarm64TestArg, twoKeyx64TestArg, twoKeyarm64OtherArg, twoKeyx64OtherArg}
	argMap := NewArgMap(Sources())
	for _, arg := range gnArgs {
		argMap.AddArg(arg)
	}

	// No file name emits to stdout.
	var buffer bytes.Buffer
	argMap.EmitMarkdown(&buffer)

	actual := buffer.String()
	expected := `# GN Build Arguments

## ` + "`target_cpu = arm64, package='other/package/default'`" + `

### arm64Other
Description of arm64 arg.

**Current value for ` + "`target_cpu = arm64, package='other/package/default'`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

## ` + "`target_cpu = arm64, package='test/package/default'`" + `

### arm64
Description of arm64 arg.

**Current value for ` + "`target_cpu = arm64, package='test/package/default'`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

## ` + "`target_cpu = x64, package='other/package/default'`" + `

### x64Other
Description of x64 arg.

**Current value for ` + "`target_cpu = x64, package='other/package/default'`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

## ` + "`target_cpu = x64, package='test/package/default'`" + `

### x64
Description of x64 arg.

**Current value for ` + "`target_cpu = x64, package='test/package/default'`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

`
	if expected != actual {
		t.Fatalf("In TestUnique, expected \n%s but got \n%s", expected, actual)
	}
}

func TestValueNewLine(t *testing.T) {

	gnArgs := []Arg{newLineValueArg}
	argMap := NewArgMap(Sources())
	for _, arg := range gnArgs {
		argMap.AddArg(arg)
	}

	// No file name emits to stdout.
	var buffer bytes.Buffer
	argMap.EmitMarkdown(&buffer)

	actual := buffer.String()
	expected := `# GN Build Arguments

## All builds

### NewLine
Description of newline arg.

**Current value (from the default):**
` + "```" + `
{
  base = "//build/toolchain/fuchsia:x64"
}
` + "```" + `

`
	if expected != actual {
		t.Fatalf("In TestDefault, expected \n%s but got \n%s", expected, actual)
	}
}
