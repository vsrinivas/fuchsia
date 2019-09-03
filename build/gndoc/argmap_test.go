// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gndoc

import (
	"bytes"
	"math/rand"
	"testing"
	"time"
)

func genArgMapInRandomOrder() *ArgMap {
	testArgMap := NewArgMap(Sources())
	gnArgs := []Arg{
		defaultx64Arg,
		defaultarm64Arg,
		defaultarm64ArgWithCurrent,
		defaultx64ArgWithCurrent,
		x64Arg,
		arm64Arg,
		twoKeyarm64TestArg,
		twoKeyx64TestArg,
		twoKeyarm64OtherArg,
		twoKeyx64OtherArg,
		newLineValueArg,
	}
	// Shuffle the gnArgs.
	r := rand.New(rand.NewSource(time.Now().Unix()))
	shuffledGnArgs := make([]Arg, 0)
	for _, j := range r.Perm(len(gnArgs)) {
		shuffledGnArgs = append(shuffledGnArgs, gnArgs[j])
	}
	for _, arg := range shuffledGnArgs {
		testArgMap.AddArg(arg)
	}
	return testArgMap
}

func TestArgMapEmitMarkdown(t *testing.T) {
	expectedOutput := `# GN Build Arguments

## ` + "`target_cpu = arm64, package='other/package/default'`" + `

### arm64Other
Description of arm64 arg.

**Current value for ` + "`target_cpu = arm64, package='other/package/default'`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

## ` + "`target_cpu = arm64, target_cpu = arm64, package='test/package/default'`" + `

### arm64
Description of arm64 arg.

**Current value for ` + "`target_cpu = arm64`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

**Current value for ` + "`target_cpu = arm64, package='test/package/default'`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

## ` + "`target_cpu = arm64, target_cpu = x64`" + `

### default
Description of default arg.

**Current value (from the default):** ` + "`false`" + `

From //test/BUILD.gn:2

### default_current
Description of default_current arg.

**Current value for ` + "`target_cpu = arm64`:** `[1, 2]`" + `

From [//build/BUILD.gn:24](http://fuchsia.com/build/BUILD.gn#24)

**Overridden from the default:** ` + "`[3, 4]`" + `

From [//base/BUILD.gn:4](http://fuchsia.com/base/BUILD.gn#4)

**Current value for ` + "`target_cpu = x64`:** `3`" + `

From [//build/BUILD.gn:24](http://fuchsia.com/build/BUILD.gn#24)

**Overridden from the default:** ` + "`4`" + `

From [//base/BUILD.gn:2](http://fuchsia.com/base/BUILD.gn#2)

## ` + "`target_cpu = x64, package='other/package/default'`" + `

### NewLine
Description of newline arg.

**Current value (from the default):**
` + "```" + `
{
  base = "//build/toolchain/fuchsia:x64"
}
` + "```" + `

### x64Other
Description of x64 arg.

**Current value for ` + "`target_cpu = x64, package='other/package/default'`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

## ` + "`target_cpu = x64, target_cpu = x64, package='test/package/default'`" + `

### x64
Description of x64 arg that references [//build/path.py](http://fuchsia.com/build/path.py), //sources, and [//base](http://fuchsia.com/base).

**Current value for ` + "`target_cpu = x64`:** `1`" + `

**Overridden from the default:** ` + "`2`" + `

**Current value for ` + "`target_cpu = x64, package='test/package/default'`:** `arg`" + `

**Overridden from the default:** ` + "`value`" + `

`
	for i := 0; i < 10; i++ {
		testArgMap := genArgMapInRandomOrder()
		var testOutput bytes.Buffer
		testArgMap.EmitMarkdown(&testOutput)
		if expectedOutput != testOutput.String() {
			t.Errorf("expecting output:\n%s\n, got:\n%s\n", expectedOutput, testOutput.String())
			break
		}
	}
}
