// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import "strings"

type All struct {
	EncodeSuccess []EncodeSuccess
	DecodeSuccess []DecodeSuccess
	EncodeFailure []EncodeFailure
	DecodeFailure []DecodeFailure
	Benchmark     []Benchmark
}

type EncodeSuccess struct {
	Name              string
	Value             interface{}
	Encodings         []Encoding
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
	// Handles
}

type DecodeSuccess struct {
	Name              string
	Value             interface{}
	Encodings         []Encoding
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
	// Handles
}

type EncodeFailure struct {
	Name              string
	Value             interface{}
	WireFormats       []WireFormat
	Err               ErrorCode
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
}

type DecodeFailure struct {
	Name              string
	Type              string
	Encodings         []Encoding
	Err               ErrorCode
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
}

type Benchmark struct {
	Name                     string
	Value                    interface{}
	BindingsAllowlist        *LanguageList
	BindingsDenylist         *LanguageList
	EnableSendEventBenchmark bool
	EnableEchoCallBenchmark  bool
}

type LanguageList []string

func (list LanguageList) Includes(targetLanguage string) bool {
	for _, language := range list {
		if language == targetLanguage {
			return true
		}
	}
	return false
}

type Encoding struct {
	WireFormat WireFormat
	Bytes      []byte
}

type WireFormat string

const (
	V1WireFormat WireFormat = "v1"
)

func (wf WireFormat) String() string {
	return string(wf)
}

type WireFormatList []WireFormat

func (list WireFormatList) Includes(wireFormat WireFormat) bool {
	for _, wf := range list {
		if wf == wireFormat {
			return true
		}
	}
	return false
}

func (list WireFormatList) Join(sep string) string {
	var b strings.Builder
	for i, wf := range list {
		if i != 0 {
			b.WriteString(sep)
		}
		b.WriteString(string(wf))
	}
	return b.String()
}
