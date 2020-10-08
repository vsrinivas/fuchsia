// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

// List of bindings that are on the deny list by default.
// Add them to an allowlist to enable.
var DefaultBindingsDenylist = []string{"reference"}

// A config passed to generators.
type GeneratorConfig struct {
	// Name for the fidl library used in rust benchmarks.
	RustBenchmarksFidlLibrary string
	// Name for the fidl library used in cpp benchmarks.
	CppBenchmarksFidlLibrary string
}
