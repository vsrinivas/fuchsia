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

func HandleRightsByName(rightsName string) (fidl.HandleRights, bool) {
	switch rightsName {
	case "none":
		return 0, true
	case "duplicate":
		return 1 << 0, true
	case "transfer":
		return 1 << 1, true
	case "read":
		return 1 << 2, true
	case "write":
		return 1 << 3, true
	case "execute":
		return 1 << 4, true
	case "map":
		return 1 << 5, true
	case "get_property":
		return 1 << 6, true
	case "set_property":
		return 1 << 7, true
	case "enumerate":
		return 1 << 8, true
	case "destroy":
		return 1 << 9, true
	case "set_policy":
		return 1 << 10, true
	case "get_policy":
		return 1 << 11, true
	case "signal":
		return 1 << 12, true
	case "signal_peer":
		return 1 << 13, true
	case "wait":
		return 1 << 14, true
	case "inspect":
		return 1 << 15, true
	case "manage_job":
		return 1 << 16, true
	case "manage_process":
		return 1 << 17, true
	case "manage_thread":
		return 1 << 18, true
	case "apply_profile":
		return 1 << 19, true

	case "same_rights":
		return 1 << 31, true

	case "basic":
		return combinedHandleRights("transfer", "duplicate", "wait", "inspect"), true
	case "io":
		return combinedHandleRights("read", "write"), true
	case "channel_default":
		return combinedHandleRights("transfer", "wait", "inspect", "io", "signal", "signal_peer"), true
	case "event_default":
		return combinedHandleRights("basic", "signal"), true
	}

	return 0, false
}

func combinedHandleRights(rightsNames ...string) fidl.HandleRights {
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
