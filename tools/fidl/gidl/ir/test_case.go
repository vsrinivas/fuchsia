// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ir

type All struct {
	EncodeSuccess []EncodeSuccess
	DecodeSuccess []DecodeSuccess
	EncodeFailure []EncodeFailure
	DecodeFailure []DecodeFailure
}

type EncodeSuccess struct {
	Name              string
	Value             interface{}
	Bytes             []byte
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
	// Handles
}

type DecodeSuccess struct {
	Name              string
	Value             interface{}
	Bytes             []byte
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
	// Handles
}

type EncodeFailure struct {
	Name              string
	Value             interface{}
	Err               ErrorCode
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
}

type DecodeFailure struct {
	Name              string
	Type              string
	Bytes             []byte
	Err               ErrorCode
	BindingsAllowlist *[]string
	BindingsDenylist  *[]string
}
