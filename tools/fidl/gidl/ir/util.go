// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"reflect"

	"go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
)

func Merge(input []All) All {
	var output All
	for _, elem := range input {
		for _, encodeSuccess := range elem.EncodeSuccess {
			output.EncodeSuccess = append(output.EncodeSuccess, encodeSuccess)
		}
		for _, decodeSuccess := range elem.DecodeSuccess {
			output.DecodeSuccess = append(output.DecodeSuccess, decodeSuccess)
		}
		for _, encodeFailure := range elem.EncodeFailure {
			output.EncodeFailure = append(output.EncodeFailure, encodeFailure)
		}
		for _, decodeFailure := range elem.DecodeFailure {
			output.DecodeFailure = append(output.DecodeFailure, decodeFailure)
		}
		for _, benchmark := range elem.Benchmark {
			output.Benchmark = append(output.Benchmark, benchmark)
		}
	}
	return output
}

func FilterByBinding(input All, binding string) All {
	shouldKeep := func(binding string, allowlist *LanguageList, denylist *LanguageList) bool {
		if denylist != nil && denylist.Includes(binding) {
			return false
		}
		if allowlist != nil {
			return allowlist.Includes(binding)
		}
		if LanguageList(config.DefaultBindingsDenylist).Includes(binding) {
			return false
		}
		return true
	}
	var output All
	for _, def := range input.EncodeSuccess {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.EncodeSuccess = append(output.EncodeSuccess, def)
		}
	}
	for _, def := range input.DecodeSuccess {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.DecodeSuccess = append(output.DecodeSuccess, def)
		}
	}
	for _, def := range input.EncodeFailure {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.EncodeFailure = append(output.EncodeFailure, def)
		}
	}
	for _, def := range input.DecodeFailure {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.DecodeFailure = append(output.DecodeFailure, def)
		}
	}
	for _, def := range input.Benchmark {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.Benchmark = append(output.Benchmark, def)
		}
	}
	return output
}

func ValidateAllType(input All, generatorType string) {
	forbid := func(fields ...interface{}) {
		for _, field := range fields {
			if reflect.ValueOf(field).Len() > 0 {
				panic("illegal field specified")
			}
		}
	}
	switch generatorType {
	case "conformance":
		forbid(input.Benchmark)
	case "benchmark":
		forbid(input.EncodeSuccess, input.DecodeSuccess, input.EncodeFailure, input.DecodeFailure)
	default:
		panic("unknown case: expected 'conformance' or 'benchmark'")
	}
}

// ContainsUnknownField returns if the value or any subvalue contains an unknown
// field.
// Intended to allow bindings that don't support unknown fields to skip test
// cases that contain them.
func ContainsUnknownField(value Value) bool {
	switch value := value.(type) {
	case Record:
		for _, f := range value.Fields {
			if f.Key.IsUnknown() {
				return true
			}
			if ContainsUnknownField(f.Value) {
				return true
			}
		}
		return false
	case []interface{}:
		for _, v := range value {
			if ContainsUnknownField(v) {
				return true
			}
		}
		return false
	default:
		return false
	}
}

func TypeFromValue(value Value) string {
	record, ok := value.(Record)
	if !ok {
		panic("only can extract type name from struct value")
	}
	return record.Name
}
