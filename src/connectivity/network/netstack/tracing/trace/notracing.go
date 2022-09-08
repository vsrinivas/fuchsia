// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !tracing

package trace

type EventScope int
type Arg struct{}
type FlowID uint64
type CounterID uint64
type AsyncID uint64
type BlobType int

func Instant(category, name string, scope EventScope) {
}

func Counter(category, name string, counterID CounterID) {
}

func DurationBegin(category, name string) {
}

func DurationEnd(category, name string) {
}

func AsyncBegin(category, name string, asyncID AsyncID) {
}

func AsyncInstant(category, name string, asyncID AsyncID) {
}

func AsyncEnd(category, name string, asyncID AsyncID) {
}

func FlowBegin(category, name string, flowID FlowID) {
}

func FlowStep(category, name string, flowID FlowID) {
}

func FlowEnd(category, name string, flowID FlowID) {
}

func Blob(typ BlobType, name string, blob []byte) {
}
