// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tools
// +build tools

package imports

import (
	// //build/secondary/third_party/golibs/google.golang.org/protobuf/cmd/protoc-gen-go
	_ "google.golang.org/protobuf/cmd/protoc-gen-go"

	// //build/secondary/third_party/golibs/google.golang.org/grpc/cmd/protoc-gen-go-grpc
	_ "google.golang.org/grpc/cmd/protoc-gen-go-grpc"
)
