// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

// TODO(fxb/39159): Remove post migration.

// ConvertUnionToXUnion converts a union to a xunion. This should only be used
// once we're already moved to the v1 format.
func ConvertUnionToXUnion(union Union) XUnion {
	return XUnion{
		Attributes:  union.Attributes,
		Name:        union.Name,
		Members:     union.Members,
		TypeShapeV1: union.TypeShapeV1,
		Strictness:  union.Strictness,
	}
}
