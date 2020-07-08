// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package config

import (
	"log"
)

type Binding string

const (
	HLCPP Binding = "cpp"
	LLCPP Binding = "llcpp"
	Dart  Binding = "dart"
	Rust  Binding = "rust"
	Go    Binding = "go"
	// GIDL only. Uses "LLCPP" for FIDL inputs.
	Walker    Binding = "walker"
	Reference Binding = "reference"
)

type ConfigKey string

type Config map[ConfigKey]interface{}

func (c Config) Get(key ConfigKey) interface{} {
	if val, ok := c[key]; ok {
		return val
	}
	log.Fatalf("key %s missing from map %#v", key, c)
	panic("")
}

func (c Config) GetInt(key ConfigKey) int {
	if val, ok := c.Get(key).(int); ok {
		return val
	}
	log.Fatalf("key %s is type %T, expected int", key, c.Get(key))
	panic("")
}

type GidlFile struct {
	Filename   string
	Gen        func(Config) (string, error)
	Benchmarks []Benchmark
}

type Benchmark struct {
	Name                     string
	Comment                  string
	Config                   Config
	Allowlist                []Binding
	Denylist                 []Binding
	EnableSendEventBenchmark bool
	EnableEchoCallBenchmark  bool
}

type FidlFile struct {
	Filename        string
	Gen             func(Config) (string, error)
	ExtraDefinition string
	Definitions     []Definition
}

type Definition struct {
	Comment  string
	Config   Config
	Denylist []Binding
}
