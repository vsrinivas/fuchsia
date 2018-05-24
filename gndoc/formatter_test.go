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
	defaultx64GNArg = GNArg{
		Name: "default",
		DefaultVal: ArgValue{
			Val:  false,
			File: "//build/.gn",
			Line: 2,
		},
		Comment: "Description of default arg.\n",
		Key:     "target_cpu = x64",
	}

	defaultarm64GNArg = GNArg{
		Name: "default",
		DefaultVal: ArgValue{
			Val:  false,
			File: "//build/.gn",
			Line: 2,
		},
		Comment: "Description of default arg.\n",
		Key:     "target_cpu = arm64",
	}

	defaultarm64GNArgWithCurrent = GNArg{
		Name: "default_current",
		CurrentVal: ArgValue{
			Val:  "[1, 2]",
			File: "//build/.gn",
			Line: 24,
		},
		DefaultVal: ArgValue{
			Val:  "[3, 4]",
			File: "//base/.gn",
			Line: 2,
		},
		Comment: "Description of default_current arg.\n",
		Key:     "target_cpu = arm64",
	}

	defaultx64GNArgWithCurrent = GNArg{
		Name: "default_current",
		CurrentVal: ArgValue{
			Val:  3,
			File: "//build/.gn",
			Line: 24,
		},
		DefaultVal: ArgValue{
			Val:  4,
			File: "//base/.gn",
			Line: 2,
		},
		Comment: "Description of default_current arg.\n",
		Key:     "target_cpu = x64",
	}

	x64GNArg = GNArg{
		Name: "x64",
		CurrentVal: ArgValue{
			Val: 1,
		},
		DefaultVal: ArgValue{
			Val: 2,
		},
		Comment: "Description of x64 arg that references //build/path.py, //sources, and //base.\n",
		Key:     "target_cpu = x64",
	}

	arm64GNArg = GNArg{
		Name: "arm64",
		CurrentVal: ArgValue{
			Val: "arg",
		},
		DefaultVal: ArgValue{
			Val: "value",
		},
		Comment: "Description of arm64 arg.\n",
		Key:     "target_cpu = arm64",
	}
)

func Sources() *SourceMap {
	s := SourceMap(make(map[string]string))
	s["base"] = "http://fuchsia.com/base"
	s["build"] = "http://fuchsia.com/build"
	return &s
}

func TestDefault(t *testing.T) {

	gnArgs := []GNArg{defaultx64GNArg, defaultarm64GNArg}
	argMap := NewArgMap(numKeys, Sources())
	argMap.allKeys = append(argMap.allKeys, "target_cpu = arm64", "target_cpu = x64")
	for _, arg := range gnArgs {
		argMap.addArg(arg, numKeys)
	}

	// No file name emits to stdout.
	var buffer bytes.Buffer
	argMap.EmitMarkdown(&buffer)

	actual := buffer.String()
	expected := `# GN Build Arguments

### default
Description of default arg.

**Default value:** [false](http://fuchsia.com/build/.gn#2)

`
	if expected != actual {
		t.Fatalf("In TestDefault, expected \n%s but got \n%s", expected, actual)
	}
}

func TestDefaultWithCurrent(t *testing.T) {

	gnArgs := []GNArg{defaultx64GNArgWithCurrent, defaultarm64GNArgWithCurrent}
	argMap := NewArgMap(numKeys, Sources())
	argMap.allKeys = append(argMap.allKeys, "target_cpu = arm64", "target_cpu = x64")
	for _, arg := range gnArgs {
		argMap.addArg(arg, numKeys)
	}

	// No file name emits to stdout.
	var buffer bytes.Buffer
	argMap.EmitMarkdown(&buffer)

	actual := buffer.String()
	expected := `# GN Build Arguments

### default_current
Description of default_current arg.

**Default value for ` + "`" + `target_cpu = x64` + "`" + `:** [4](http://fuchsia.com/base/.gn#2)

**Default value for ` + "`" + `target_cpu = arm64` + "`" + `:** [[3, 4]](http://fuchsia.com/base/.gn#2)

**Current value for ` + "`" + `target_cpu = x64` + "`" + `:** [3](http://fuchsia.com/build/.gn#24)

**Current value for ` + "`" + `target_cpu = arm64` + "`" + `:** [[1, 2]](http://fuchsia.com/build/.gn#24)

`

	if expected != actual {
		t.Fatalf("In TestDefaultWithCurrent, expected \n%s but got \n%s", expected, actual)
	}
}

func TestUnique(t *testing.T) {

	gnArgs := []GNArg{x64GNArg, arm64GNArg}
	argMap := NewArgMap(numKeys, Sources())
	argMap.allKeys = append(argMap.allKeys, "target_cpu = arm64", "target_cpu = x64")
	for _, arg := range gnArgs {
		argMap.addArg(arg, numKeys)
	}

	// No file name emits to stdout.
	var buffer bytes.Buffer
	argMap.EmitMarkdown(&buffer)

	actual := buffer.String()
	expected := `# GN Build Arguments

### arm64
Description of arm64 arg.

**Default value for ` + "`" + `target_cpu = arm64` + "`" + `:** value

**Current value for ` + "`" + `target_cpu = arm64` + "`" + `:** arg

No values for ` + "`" + `target_cpu = x64` + "`" + `.

### x64
Description of x64 arg that references [//build/path.py](http://fuchsia.com/build/path.py), //sources, and [//base](http://fuchsia.com/base).

**Default value for ` + "`" + `target_cpu = x64` + "`" + `:** 2

**Current value for ` + "`" + `target_cpu = x64` + "`" + `:** 1

No values for ` + "`" + `target_cpu = arm64` + "`" + `.
`
	if expected != actual {
		t.Fatalf("In TestUnique, expected \n%s but got \n%s", expected, actual)
	}
}
