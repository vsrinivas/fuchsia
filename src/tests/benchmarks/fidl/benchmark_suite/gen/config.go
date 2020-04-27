// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

type Binding string

const (
	HLCPP Binding = "cpp"
	LLCPP Binding = "llcpp"
	Dart  Binding = "dart"
	Rust  Binding = "rust"
	Go    Binding = "go"
)

type ConfigKey string

const (
	Size ConfigKey = "size"
)

type Config map[ConfigKey]interface{}

type GidlFile struct {
	Filename   string
	Gen        func(Config) (string, error)
	Benchmarks []Benchmark
}

type Benchmark struct {
	Name      string
	Config    Config
	Allowlist []Binding
	Denylist  []Binding
}

type FidlFile struct {
	Filename    string
	Gen         func(Config) (string, error)
	Definitions []Definition
}

type Definition struct {
	Config Config
}
