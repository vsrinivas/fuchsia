// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"fmt"
	"os"
	"runtime/debug"
)

// TemplateFatalf exits the program with a formatted error message similar to
// Printf. It is meant to be used by helper functions invoked by the template
// engine, since normal panics are swallowed by it.
func TemplateFatalf(format string, a ...interface{}) {
	fmt.Printf(format, a...)
	debug.PrintStack()
	os.Exit(1)
}
