// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

// BoolSlice
type BoolSlice []bool

func (s BoolSlice) Len() int {
	return len(s)
}

func (s BoolSlice) Less(i, j int) bool {
	return s[i] && !s[j]
}

func (s BoolSlice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// Float32Slice
type Float32Slice []float32

func (s Float32Slice) Len() int {
	return len(s)
}

func (s Float32Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s Float32Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// Int8Slice
type Int8Slice []int8

func (s Int8Slice) Len() int {
	return len(s)
}

func (s Int8Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s Int8Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// Int16Slice
type Int16Slice []int16

func (s Int16Slice) Len() int {
	return len(s)
}

func (s Int16Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s Int16Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// Int32Slice
type Int32Slice []int32

func (s Int32Slice) Len() int {
	return len(s)
}

func (s Int32Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s Int32Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// Int64Slice
type Int64Slice []int64

func (s Int64Slice) Len() int {
	return len(s)
}

func (s Int64Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s Int64Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// UInt8Slice
type UInt8Slice []uint8

func (s UInt8Slice) Len() int {
	return len(s)
}

func (s UInt8Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s UInt8Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// UInt16Slice
type UInt16Slice []uint16

func (s UInt16Slice) Len() int {
	return len(s)
}

func (s UInt16Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s UInt16Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// UInt32Slice
type UInt32Slice []uint32

func (s UInt32Slice) Len() int {
	return len(s)
}

func (s UInt32Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s UInt32Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// UInt64Slice
type UInt64Slice []uint64

func (s UInt64Slice) Len() int {
	return len(s)
}

func (s UInt64Slice) Less(i, j int) bool {
	return s[i] < s[j]
}

func (s UInt64Slice) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}
