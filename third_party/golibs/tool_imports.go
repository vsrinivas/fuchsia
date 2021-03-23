// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build tools

package imports

import (
	// //build/secondary/third_party/golibs/google.golang.org/protobuf/cmd/protoc-gen-go
	_ "google.golang.org/protobuf/cmd/protoc-gen-go"

	// //build/secondary/third_party/golibs/google.golang.org/grpc/cmd/protoc-gen-go-grpc
	_ "google.golang.org/grpc/cmd/protoc-gen-go-grpc"

	// //third_party/cobalt/src/registry/annotations.proto imports descriptor.proto.
	//
	// TODO(https://fxbug.dev/70570): Remove this when we're on protoc 3.14.0 or higher.
	_ "github.com/golang/protobuf/protoc-gen-go/descriptor"
)
