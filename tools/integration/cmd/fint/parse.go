// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"github.com/golang/protobuf/proto"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/cmd/fint/proto"
)

func parseStatic(text string) (*fintpb.Static, error) {
	message := &fintpb.Static{}
	if err := proto.UnmarshalText(text, message); err != nil {
		return nil, err
	}
	return message, nil
}

func parseContext(text string) (*fintpb.Context, error) {
	message := &fintpb.Context{}
	if err := proto.UnmarshalText(text, message); err != nil {
		return nil, err
	}
	return message, nil
}
