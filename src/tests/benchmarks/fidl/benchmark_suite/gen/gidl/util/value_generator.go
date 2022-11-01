// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package util

import (
	"fmt"
	"math/rand"
	"strconv"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/types"
)

func RandomValues(typ types.FidlType) ValueGenerator {
	return &randomValueGenerator{
		typ: typ,
		r:   rand.New(rand.NewSource(0)),
	}
}

func SequentialValues(typ types.FidlType, start int) ValueGenerator {
	return &sequentialValueGenerator{
		typ:     typ,
		format:  "%d",
		nextVal: uint64(start),
	}
}

func SequentialHexValues(typ types.FidlType, start int) ValueGenerator {
	return &sequentialValueGenerator{
		typ:     typ,
		format:  "%#02x",
		nextVal: uint64(start),
	}
}

type ValueGenerator interface {
	next() string
}

type randomValueGenerator struct {
	typ types.FidlType
	r   *rand.Rand
}

func (gen *randomValueGenerator) next() string {
	switch gen.typ {
	case types.Uint8:
		return strconv.FormatUint(uint64(uint8(gen.r.Uint32())), 10)
	case types.Uint16:
		return strconv.FormatUint(uint64(uint16(gen.r.Uint32())), 10)
	case types.Uint32:
		return strconv.FormatUint(uint64(gen.r.Uint32()), 10)
	case types.Uint64:
		return strconv.FormatUint(gen.r.Uint64(), 10)
	case types.Int8:
		return strconv.FormatInt(int64(int8(gen.r.Uint32())), 10)
	case types.Int16:
		return strconv.FormatInt(int64(int16(gen.r.Uint32())), 10)
	case types.Int32:
		return strconv.FormatInt(int64(uint32(gen.r.Uint32())), 10)
	case types.Int64:
		return strconv.FormatInt(int64(gen.r.Uint64()), 10)
	case types.Float32:
		return strconv.FormatFloat(float64(gen.r.Float32()), 'f', -1, 32)
	case types.Float64:
		return strconv.FormatFloat(float64(gen.r.Float64()), 'f', -1, 32)
	default:
		panic("not supported")
	}
}

type sequentialValueGenerator struct {
	typ     types.FidlType
	format  string
	nextVal uint64
}

func (gen *sequentialValueGenerator) next() string {
	lastVal := gen.nextVal
	gen.nextVal += 1

	var typedValue interface{}
	switch gen.typ {
	case types.Uint8:
		typedValue = uint8(lastVal)
	case types.Uint16:
		typedValue = uint16(lastVal)
	case types.Uint32:
		typedValue = uint32(lastVal)
	case types.Uint64:
		typedValue = lastVal
	case types.Int8:
		typedValue = int8(lastVal)
	case types.Int16:
		typedValue = int16(lastVal)
	case types.Int32:
		typedValue = int32(lastVal)
	case types.Int64:
		typedValue = int64(lastVal)
	default:
		panic("not supported")
	}
	return fmt.Sprintf(gen.format, typedValue)
}
