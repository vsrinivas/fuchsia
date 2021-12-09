// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !tracing
// +build !tracing

package trace

type EventScope int
type Arg struct{}
type FlowID uint64
type BlobType int

func Instant(category, name string, scope EventScope, args ...Arg) {
}

func DurationBegin(category, name string, args ...Arg) {
}

func DurationEnd(category, name string, args ...Arg) {
}

func FlowBegin(category, name string, flowID FlowID, args ...Arg) {
}

func FlowStep(category, name string, flowID FlowID, args ...Arg) {
}

func FlowEnd(category, name string, flowID FlowID, args ...Arg) {
}

func Blob(typ BlobType, name string, blob []byte) {
}
