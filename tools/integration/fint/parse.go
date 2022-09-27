// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"os"

	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"

	"google.golang.org/protobuf/encoding/prototext"
)

// ReadStatic deserializes a Static proto from a textproto file.
func ReadStatic(path string) (*fintpb.Static, error) {
	bytes, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var message fintpb.Static
	if err := prototext.Unmarshal(bytes, &message); err != nil {
		return nil, err
	}
	return &message, nil
}

// ReadContext deserializes a Context proto from a textproto file.
func ReadContext(path string) (*fintpb.Context, error) {
	bytes, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var message fintpb.Context
	if err := prototext.Unmarshal(bytes, &message); err != nil {
		return nil, err
	}
	return &message, nil
}

// ReadBuildArtifacts deserializes a BuildArtifacts textproto file.
func ReadBuildArtifacts(path string) (*fintpb.BuildArtifacts, error) {
	bytes, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var message fintpb.BuildArtifacts
	if err := prototext.Unmarshal(bytes, &message); err != nil {
		return nil, err
	}
	return &message, nil
}
