// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

import "fmt"

type All struct {
	EncodeSuccess []EncodeSuccess
	DecodeSuccess []DecodeSuccess
	EncodeFailure []EncodeFailure
	DecodeFailure []DecodeFailure
}

type EncodeSuccess struct {
	Name              string
	Value             interface{}
	Encodings         []Encoding
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
	// Handles
}

type DecodeSuccess struct {
	Name              string
	Value             interface{}
	Encodings         []Encoding
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
	// Handles
}

type EncodeFailure struct {
	Name              string
	Value             interface{}
	WireFormats       []WireFormat
	Err               ErrorCode
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
}

type DecodeFailure struct {
	Name              string
	Type              string
	Encodings         []Encoding
	Err               ErrorCode
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
}

type Encoding struct {
	WireFormat WireFormat
	Bytes      []byte
}

type WireFormat uint

const (
	_ WireFormat = iota
	OldWireFormat
	V1WireFormat
)

var nameToWireFormat = map[string]WireFormat{}
var wireFormatToName = map[WireFormat]string{}
var allWireFormats = func() []WireFormat {
	register := func(wf WireFormat, name string) WireFormat {
		nameToWireFormat[name] = wf
		wireFormatToName[wf] = name
		return wf
	}
	return []WireFormat{
		register(OldWireFormat, "old"),
		register(V1WireFormat, "v1"),
	}
}()

func AllWireFormats() []WireFormat {
	return append([]WireFormat(nil), allWireFormats...)
}

func (wf WireFormat) String() string {
	if name, ok := wireFormatToName[wf]; ok {
		return name
	}
	return fmt.Sprintf("unknown wire format (%d)", wf)
}

func WireFormatByName(name string) (WireFormat, error) {
	if wf, ok := nameToWireFormat[name]; ok {
		return wf, nil
	}
	return 0, fmt.Errorf("unknown wire format %q", name)
}
