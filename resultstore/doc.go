// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package resultstore is an abstraction over the ResultStore Upload API proto definitions.
//
// UploadClient is a wrapper around the ResultStoreUpload client libraries.  Each of the
// entities in this package wrap individual messages, and convert to and from their
// counterparts. These wrappers are more easily filled from user input than raw proto messages.
//
// See https://github.com/googleapis/googleapis/tree/master/google/devtools/resultstore/v2
// for more details on the proto definitions.
//
// Run 'go generate ./...' to regenerate mock sources in this package.
//
//go:generate mockgen -destination=mocks/proto_mocks.go -package=mocks google.golang.org/genproto/googleapis/devtools/resultstore/v2 ResultStoreUploadClient
//
// Do not forget to add the Fuchsia copyright header at the top of the generated file.
package resultstore
