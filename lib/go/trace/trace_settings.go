// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package trace provides definition and methods for TraceProvider and
// Registers it with TraceRegistry
package trace

import (
	"flag"
)

var flags struct {
	TraceLabel string
}

func init() {
	flag.StringVar(&flags.TraceLabel, "trace-label", "", "Identifies the provider. If empty name of current process is used.")
}

type Setting struct {
	// The label to present to the user to identify the provider.
	// If empty, uses the name of the current process.
	ProviderLabel string
}

// Return new trace setting using command-line flag -trace-label
func ParseTraceSettingsUsingFlag(ts Setting) (error, Setting) {
	if !flag.Parsed() {
		flag.Parse()
	}
	if flags.TraceLabel != "" {
		ts.ProviderLabel = flags.TraceLabel
	}
	return nil, ts
}
