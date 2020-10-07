// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"github.com/golang/protobuf/proto"
	static "go.fuchsia.dev/fuchsia/tools/integration/cmd/fint/proto"
)

func parseStatic(text string) (*static.Static, error) {
	message := &static.Static{}
	if err := proto.UnmarshalText(text, message); err != nil {
		return nil, err
	}
	return message, nil
}
