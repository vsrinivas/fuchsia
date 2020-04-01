// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import "reflect"

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
		for _, encodeBenchmark := range elem.EncodeBenchmark {
			output.EncodeBenchmark = append(output.EncodeBenchmark, encodeBenchmark)
		}
		for _, decodeBenchmark := range elem.DecodeBenchmark {
			output.DecodeBenchmark = append(output.DecodeBenchmark, decodeBenchmark)
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
	for _, def := range input.EncodeBenchmark {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.EncodeBenchmark = append(output.EncodeBenchmark, def)
		}
	}
	for _, def := range input.DecodeBenchmark {
		if shouldKeep(binding, def.BindingsAllowlist, def.BindingsDenylist) {
			output.DecodeBenchmark = append(output.DecodeBenchmark, def)
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
		forbid(input.EncodeBenchmark, input.DecodeBenchmark)
	case "benchmark":
		forbid(input.EncodeSuccess, input.DecodeSuccess, input.EncodeFailure, input.DecodeFailure)
	default:
		panic("unknown case")
	}
}

// ContainsUnknownField returns if the value or any subvalue contains an unknown
// field.
// Intended to allow bindings that don't support unknown fields to skip test
// cases that contain them.
func ContainsUnknownField(value Value) bool {
	switch value := value.(type) {
	case Object:
		for _, f := range value.Fields {
			if f.Key.Name == "" {
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
