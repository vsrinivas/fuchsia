// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"strings"

	fidlir "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

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
	HandleDefs        []HandleDef
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
}

type DecodeSuccess struct {
	Name              string
	Value             interface{}
	Encodings         []Encoding
	HandleDefs        []HandleDef
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
}

type EncodeFailure struct {
	Name              string
	Value             interface{}
	HandleDefs        []HandleDef
	WireFormats       []WireFormat
	Err               ErrorCode
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
}

type DecodeFailure struct {
	Name              string
	Type              string
	Encodings         []Encoding
	HandleDefs        []HandleDef
	Err               ErrorCode
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
}

type Benchmark struct {
	Name                     string
	Value                    interface{}
	HandleDefs               []HandleDef
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

type HandleDef struct {
	Subtype fidlir.HandleSubtype
	// TODO(fxbug.dev/41920): Add a field for handle rights.
}

var supportedHandleSubtypes = map[fidlir.HandleSubtype]struct{}{
	fidlir.Channel: {},
	fidlir.Event:   {},
}

func HandleSubtypeByName(s string) (fidlir.HandleSubtype, bool) {
	subtype := fidlir.HandleSubtype(s)
	_, ok := supportedHandleSubtypes[subtype]
	if ok {
		return subtype, true
	}
	return "", false
}

type Encoding struct {
	WireFormat WireFormat
	Bytes      []byte
	Handles    []Handle
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
