// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package benchmark_suite

import (
	"testing"

	"syscall/zx/fidl"
)

type Int64Struct struct {
	_ struct{} `fidl:"s" fidl_size_v1:"8" fidl_alignment_v1:"8"`
	X int64    `fidl_offset_v1:"0"`
}

var _mInt64Struct = fidl.CreateLazyMarshaler(Int64Struct{})

func (msg *Int64Struct) Marshaler() fidl.Marshaler {
	return _mInt64Struct
}

// Benchmark encoding a FIDL struct containing an int64.
func BenchmarkEncodeInt64Struct(b *testing.B) {
	data := make([]byte, 1024)
	input := &Int64Struct{}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, _, err := fidl.Marshal(input, data, nil)
		if err != nil {
			b.Fatal(err)
		}
	}
}

// Benchmark decoding a FIDL struct containing an int64.
func BenchmarkDecodeInt64Struct(b *testing.B) {
	data := make([]byte, 1024)
	input := &Int64Struct{}
	_, _, err := fidl.Marshal(input, data, nil)
	if err != nil {
		b.Fatal(err)
	}
	output := &Int64Struct{}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, _, err := fidl.Unmarshal(data, nil, output)
		if err != nil {
			b.Fatal(err)
		}
	}
}

type Benchmark struct {
	Label     string
	BenchFunc func(*testing.B)
}

var Benchmarks = []Benchmark{
	{
		"Encode/Int64Struct",
		BenchmarkEncodeInt64Struct,
	},
	{
		"Decode/Int64Struct",
		BenchmarkDecodeInt64Struct,
	},
}
