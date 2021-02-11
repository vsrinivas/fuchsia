// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"strings"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
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
	Subtype fidl.HandleSubtype
	Rights  fidl.HandleRights
}

var supportedHandleSubtypes = map[fidl.HandleSubtype]struct{}{
	fidl.Channel: {},
	fidl.Event:   {},
}

func HandleSubtypeByName(s string) (fidl.HandleSubtype, bool) {
	subtype := fidl.HandleSubtype(s)
	_, ok := supportedHandleSubtypes[subtype]
	if ok {
		return subtype, true
	}
	return "", false
}

// handleRightsByName is initialized in two phases, constants here, and combined
// rights in `init`.
var handleRightsByName = map[string]fidl.HandleRights{
	"none":        0,
	"same_rights": 1 << 31,

	"duplicate":      1 << 0,
	"transfer":       1 << 1,
	"read":           1 << 2,
	"write":          1 << 3,
	"execute":        1 << 4,
	"map":            1 << 5,
	"get_property":   1 << 6,
	"set_property":   1 << 7,
	"enumerate":      1 << 8,
	"destroy":        1 << 9,
	"set_policy":     1 << 10,
	"get_policy":     1 << 11,
	"signal":         1 << 12,
	"signal_peer":    1 << 13,
	"wait":           1 << 14,
	"inspect":        1 << 15,
	"manage_job":     1 << 16,
	"manage_process": 1 << 17,
	"manage_thread":  1 << 18,
	"apply_profile":  1 << 19,
}

func init() {
	combinedHandleRights := func(rightsNames ...string) fidl.HandleRights {
		var combinedRights fidl.HandleRights
		for _, rightsName := range rightsNames {
			rights, ok := HandleRightsByName(rightsName)
			if !ok {
				panic("bug in specifying combined rights: unknown name")
			}
			combinedRights |= rights
		}
		return combinedRights
	}
	handleRightsByName["basic"] = combinedHandleRights("transfer", "duplicate", "wait", "inspect")
	handleRightsByName["io"] = combinedHandleRights("read", "write")
	handleRightsByName["channel_default"] = combinedHandleRights("transfer", "wait", "inspect", "io", "signal", "signal_peer")
	handleRightsByName["event_default"] = combinedHandleRights("basic", "signal")

}

func HandleRightsByName(rightsName string) (fidl.HandleRights, bool) {
	rights, ok := handleRightsByName[rightsName]
	return rights, ok
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
