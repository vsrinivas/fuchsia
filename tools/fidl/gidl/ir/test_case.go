// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import (
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
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
	Value             Record
	Encodings         []HandleDispositionEncoding
	HandleDefs        []HandleDef
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
	// CheckHandleRights is true for standalone "encode_success" tests providing
	// "handle_dispositions", but false for bidirectional "success" tests
	// because they provide only "handles" with no rights information.
	CheckHandleRights bool
}

type DecodeSuccess struct {
	Name              string
	Value             Record
	Encodings         []Encoding
	HandleDefs        []HandleDef
	BindingsAllowlist *LanguageList
	BindingsDenylist  *LanguageList
}

type EncodeFailure struct {
	Name              string
	Value             Record
	HandleDefs        []HandleDef
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
	Value                    Record
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
	Subtype fidlgen.HandleSubtype
	Rights  fidlgen.HandleRights
}

var supportedHandleSubtypes = map[fidlgen.HandleSubtype]struct{}{
	fidlgen.Channel: {},
	fidlgen.Event:   {},
}

func HandleSubtypeByName(s string) (fidlgen.HandleSubtype, bool) {
	subtype := fidlgen.HandleSubtype(s)
	_, ok := supportedHandleSubtypes[subtype]
	if ok {
		return subtype, true
	}
	return "", false
}

// handleRightsByName is initialized in two phases, constants here, and combined
// rights in `init`.
var handleRightsByName = map[string]fidlgen.HandleRights{
	"none":        fidlgen.HandleRightsNone,
	"same_rights": fidlgen.HandleRightsSameRights,

	"duplicate":      fidlgen.HandleRightsDuplicate,
	"transfer":       fidlgen.HandleRightsTransfer,
	"read":           fidlgen.HandleRightsRead,
	"write":          fidlgen.HandleRightsWrite,
	"execute":        fidlgen.HandleRightsExecute,
	"map":            fidlgen.HandleRightsMap,
	"get_property":   fidlgen.HandleRightsGetProperty,
	"set_property":   fidlgen.HandleRightsSetProperty,
	"enumerate":      fidlgen.HandleRightsEnumerate,
	"destroy":        fidlgen.HandleRightsDestroy,
	"set_policy":     fidlgen.HandleRightsSetPolicy,
	"get_policy":     fidlgen.HandleRightsGetPolicy,
	"signal":         fidlgen.HandleRightsSignal,
	"signal_peer":    fidlgen.HandleRightsSignalPeer,
	"wait":           fidlgen.HandleRightsWait,
	"inspect":        fidlgen.HandleRightsInspect,
	"manage_job":     fidlgen.HandleRightsManageJob,
	"manage_process": fidlgen.HandleRightsManageProcess,
	"manage_thread":  fidlgen.HandleRightsManageThread,
	"apply_profile":  fidlgen.HandleRightsApplyProfile,
}

func init() {
	combinedHandleRights := func(rightsNames ...string) fidlgen.HandleRights {
		var combinedRights fidlgen.HandleRights
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

func HandleRightsByName(rightsName string) (fidlgen.HandleRights, bool) {
	rights, ok := handleRightsByName[rightsName]
	return rights, ok
}

type HandleDisposition struct {
	Handle Handle
	Type   fidlgen.ObjectType
	Rights fidlgen.HandleRights
}

type Encoding struct {
	WireFormat WireFormat
	Bytes      []byte
	Handles    []Handle
}

type HandleDispositionEncoding struct {
	WireFormat         WireFormat
	Bytes              []byte
	HandleDispositions []HandleDisposition
}

type WireFormat string

const (
	V1WireFormat WireFormat = "v1"
	V2WireFormat WireFormat = "v2"
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
