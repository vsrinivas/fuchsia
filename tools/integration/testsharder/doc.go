// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package testsharder creates shards based on tests and specified environments.
//
// Test authors in the Fuchsia source will specify `environments`in GN within
// the packages they define their tests. These specifications will be printed
// to disk in JSON-form during a build.
//
// This package is concerned with reading in those specifications, validating
// that they correspond to valid test environments supported by the
// infrastructure, and ultimately sharding the associated tests along the lines
// of those environments.

package testsharder
