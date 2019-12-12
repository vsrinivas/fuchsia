// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

// TODO(fxb/39159): Remove post migration.

// ConvertUnionToXUnion converts a union to a xunion. This should only be used
// once we're already moved to the v1 format.
func ConvertUnionToXUnion(union Union) XUnion {
	var members []XUnionMember
	for _, member := range union.Members {
		if member.Reserved {
			continue
		}
		members = append(members, XUnionMember{
			Attributes:      member.Attributes,
			Ordinal:         member.XUnionOrdinal,
			ExplicitOrdinal: member.XUnionOrdinal,
			// This field doesn't apply to unions that were xunions since they
			// already use explicit ordinals on the wire. Since backends only use
			// this field to generate code that reads xunions, passing in the
			// explicit ordinal here is OK.
			HashedOrdinal: member.XUnionOrdinal,
			Type:          member.Type,
			Name:          member.Name,
			Offset:        -1, // unused
			MaxOutOfLine:  -1, // unused
		})
	}
	typeShape := TypeShape{
		InlineSize:          24,
		Alignment:           8,
		Depth:               union.TypeShapeV1.Depth,
		MaxHandles:          union.TypeShapeV1.MaxHandles,
		MaxOutOfLine:        union.TypeShapeV1.MaxOutOfLine,
		HasPadding:          union.TypeShapeV1.HasPadding,
		HasFlexibleEnvelope: union.TypeShapeV1.HasFlexibleEnvelope,
		ContainsUnion:       union.TypeShapeV1.ContainsUnion,
	}
	return XUnion{
		Attributes:      union.Attributes,
		Name:            union.Name,
		Members:         members,
		Size:            typeShape.InlineSize,
		Alignment:       typeShape.Alignment,
		MaxHandles:      typeShape.MaxHandles,
		MaxOutOfLine:    typeShape.MaxOutOfLine,
		TypeShapeOld:    typeShape,
		TypeShapeV1:     typeShape,
		TypeShapeV1NoEE: typeShape,
		Strictness:      true,
	}
}
