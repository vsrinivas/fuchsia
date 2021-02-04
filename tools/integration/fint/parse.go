// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"io/ioutil"

	"github.com/golang/protobuf/proto"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
)

// ReadStatic deserializes a Static proto from a textproto file.
func ReadStatic(path string) (*fintpb.Static, error) {
	message := &fintpb.Static{}
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if err := proto.UnmarshalText(string(bytes), message); err != nil {
		return nil, err
	}
	return message, nil
}

// ReadContext deserializes a Context proto from a textproto file.
func ReadContext(path string) (*fintpb.Context, error) {
	message := &fintpb.Context{}
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if err := proto.UnmarshalText(string(bytes), message); err != nil {
		return nil, err
	}
	return message, nil
}
