// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use pretty_assertions::assert_eq;
use serde_json;
use std::collections::BTreeMap;

#[test]
fn fidl_ir() {
    use banjo_lib::fidl;
    let input = include_str!("fidl/test.fidl.json");
    let mut decls = BTreeMap::new();
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/Int64Struct".to_string()),
        fidl::Declaration::Struct,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/HasOptionalFieldStruct".to_string()),
        fidl::Declaration::Struct,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/Has2OptionalFieldStruct".to_string()),
        fidl::Declaration::Struct,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/Empty".to_string()),
        fidl::Declaration::Struct,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/EmptyStructSandwich".to_string()),
        fidl::Declaration::Struct,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/InlineXUnionInStruct".to_string()),
        fidl::Declaration::Struct,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/OptionalXUnionInStruct".to_string()),
        fidl::Declaration::Struct,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/SimpleTable".to_string()),
        fidl::Declaration::Table,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/OlderSimpleTable".to_string()),
        fidl::Declaration::Table,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/NewerSimpleTable".to_string()),
        fidl::Declaration::Table,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/XUnionInTable".to_string()),
        fidl::Declaration::Table,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/SimpleUnion".to_string()),
        fidl::Declaration::Union,
    );
    decls.insert(
        fidl::CompoundIdentifier("fidl.test.misc/SampleXUnion".to_string()),
        fidl::Declaration::XUnion,
    );
    let decls = decls;

    let expected = fidl::Ir {
        version: fidl::Version("0.0.1".to_string()),
        name: fidl::LibraryIdentifier("fidl.test.misc".to_string()),
        const_declarations: vec![],
        enum_declarations: vec![],
        interface_declarations: vec![],
        struct_declarations: vec![
            fidl::Struct {
                max_handles: Some(fidl::Count(0)),
                maybe_attributes: None,
                name: fidl::CompoundIdentifier("fidl.test.misc/Int64Struct".to_string()),
                location: Some(fidl::Location {
                    filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                    line: 7,
                    column: 7,
                }),
                anonymous: Some(false),
                members: vec![fidl::StructMember {
                    _type: fidl::Type::Primitive { subtype: fidl::PrimitiveSubtype::Int64 },
                    name: fidl::Identifier("x".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 8,
                        column: 10,
                    }),
                    size: fidl::Count(8),
                    max_out_of_line: fidl::Count(0),
                    alignment: fidl::Count(8),
                    offset: fidl::Count(0),
                    maybe_attributes: None,
                    maybe_default_value: None,
                }],
                size: fidl::Count(8),
                max_out_of_line: fidl::Count(0),
            },
            fidl::Struct {
                max_handles: Some(fidl::Count(0)),
                maybe_attributes: None,
                name: fidl::CompoundIdentifier("fidl.test.misc/HasOptionalFieldStruct".to_string()),
                location: Some(fidl::Location {
                    filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                    line: 11,
                    column: 7,
                }),
                anonymous: Some(false),
                members: vec![fidl::StructMember {
                    _type: fidl::Type::Identifier {
                        identifier: fidl::CompoundIdentifier(
                            "fidl.test.misc/Int64Struct".to_string(),
                        ),
                        nullable: true,
                    },
                    name: fidl::Identifier("x".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 12,
                        column: 17,
                    }),
                    size: fidl::Count(8),
                    max_out_of_line: fidl::Count(8),
                    alignment: fidl::Count(8),
                    offset: fidl::Count(0),
                    maybe_attributes: None,
                    maybe_default_value: None,
                }],
                size: fidl::Count(8),
                max_out_of_line: fidl::Count(8),
            },
            fidl::Struct {
                max_handles: Some(fidl::Count(0)),
                maybe_attributes: None,
                name: fidl::CompoundIdentifier(
                    "fidl.test.misc/Has2OptionalFieldStruct".to_string(),
                ),
                location: Some(fidl::Location {
                    filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                    line: 15,
                    column: 7,
                }),
                anonymous: Some(false),
                members: vec![
                    fidl::StructMember {
                        _type: fidl::Type::Identifier {
                            identifier: fidl::CompoundIdentifier(
                                "fidl.test.misc/Int64Struct".to_string(),
                            ),
                            nullable: true,
                        },
                        name: fidl::Identifier("x".to_string()),
                        location: Some(fidl::Location {
                            filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                            line: 16,
                            column: 17,
                        }),
                        size: fidl::Count(8),
                        max_out_of_line: fidl::Count(8),
                        alignment: fidl::Count(8),
                        offset: fidl::Count(0),
                        maybe_attributes: None,
                        maybe_default_value: None,
                    },
                    fidl::StructMember {
                        _type: fidl::Type::Identifier {
                            identifier: fidl::CompoundIdentifier(
                                "fidl.test.misc/Int64Struct".to_string(),
                            ),
                            nullable: true,
                        },
                        name: fidl::Identifier("y".to_string()),
                        location: Some(fidl::Location {
                            filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                            line: 17,
                            column: 17,
                        }),
                        size: fidl::Count(8),
                        max_out_of_line: fidl::Count(8),
                        alignment: fidl::Count(8),
                        offset: fidl::Count(8),
                        maybe_attributes: None,
                        maybe_default_value: None,
                    },
                ],
                size: fidl::Count(16),
                max_out_of_line: fidl::Count(16),
            },
            fidl::Struct {
                max_handles: Some(fidl::Count(0)),
                maybe_attributes: None,
                name: fidl::CompoundIdentifier("fidl.test.misc/Empty".to_string()),
                location: Some(fidl::Location {
                    filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                    line: 20,
                    column: 7,
                }),
                anonymous: Some(false),
                members: vec![],
                size: fidl::Count(1),
                max_out_of_line: fidl::Count(0),
            },
            fidl::Struct {
                max_handles: Some(fidl::Count(0)),
                maybe_attributes: None,
                name: fidl::CompoundIdentifier("fidl.test.misc/EmptyStructSandwich".to_string()),
                location: Some(fidl::Location {
                    filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                    line: 23,
                    column: 7,
                }),
                anonymous: Some(false),
                members: vec![
                    fidl::StructMember {
                        _type: fidl::Type::Str { maybe_element_count: None, nullable: false },
                        name: fidl::Identifier("before".to_string()),
                        location: Some(fidl::Location {
                            filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                            line: 24,
                            column: 11,
                        }),
                        size: fidl::Count(16),
                        max_out_of_line: fidl::Count(4294967295),
                        alignment: fidl::Count(8),
                        offset: fidl::Count(0),
                        maybe_attributes: None,
                        maybe_default_value: None,
                    },
                    fidl::StructMember {
                        _type: fidl::Type::Identifier {
                            identifier: fidl::CompoundIdentifier(
                                "fidl.test.misc/Empty".to_string(),
                            ),
                            nullable: false,
                        },
                        name: fidl::Identifier("e".to_string()),
                        location: Some(fidl::Location {
                            filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                            line: 25,
                            column: 10,
                        }),
                        size: fidl::Count(1),
                        max_out_of_line: fidl::Count(0),
                        alignment: fidl::Count(1),
                        offset: fidl::Count(16),
                        maybe_attributes: None,
                        maybe_default_value: None,
                    },
                    fidl::StructMember {
                        _type: fidl::Type::Str { maybe_element_count: None, nullable: false },
                        name: fidl::Identifier("after".to_string()),
                        location: Some(fidl::Location {
                            filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                            line: 26,
                            column: 11,
                        }),
                        size: fidl::Count(16),
                        max_out_of_line: fidl::Count(4294967295),
                        alignment: fidl::Count(8),
                        offset: fidl::Count(24),
                        maybe_attributes: None,
                        maybe_default_value: None,
                    },
                ],
                size: fidl::Count(40),
                max_out_of_line: fidl::Count(4294967295),
            },
            fidl::Struct {
                max_handles: Some(fidl::Count(0)),
                maybe_attributes: None,
                name: fidl::CompoundIdentifier("fidl.test.misc/InlineXUnionInStruct".to_string()),
                location: Some(fidl::Location {
                    filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                    line: 70,
                    column: 7,
                }),
                anonymous: Some(false),
                members: vec![
                    fidl::StructMember {
                        _type: fidl::Type::Str { maybe_element_count: None, nullable: false },
                        name: fidl::Identifier("before".to_string()),
                        location: Some(fidl::Location {
                            filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                            line: 71,
                            column: 11,
                        }),
                        size: fidl::Count(16),
                        max_out_of_line: fidl::Count(4294967295),
                        alignment: fidl::Count(8),
                        offset: fidl::Count(0),
                        maybe_attributes: None,
                        maybe_default_value: None,
                    },
                    fidl::StructMember {
                        _type: fidl::Type::Identifier {
                            identifier: fidl::CompoundIdentifier(
                                "fidl.test.misc/SampleXUnion".to_string(),
                            ),
                            nullable: false,
                        },
                        name: fidl::Identifier("xu".to_string()),
                        location: Some(fidl::Location {
                            filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                            line: 72,
                            column: 17,
                        }),
                        size: fidl::Count(24),
                        max_out_of_line: fidl::Count(4294967295),
                        alignment: fidl::Count(8),
                        offset: fidl::Count(16),
                        maybe_attributes: None,
                        maybe_default_value: None,
                    },
                    fidl::StructMember {
                        _type: fidl::Type::Str { maybe_element_count: None, nullable: false },
                        name: fidl::Identifier("after".to_string()),
                        location: Some(fidl::Location {
                            filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                            line: 73,
                            column: 11,
                        }),
                        size: fidl::Count(16),
                        max_out_of_line: fidl::Count(4294967295),
                        alignment: fidl::Count(8),
                        offset: fidl::Count(40),
                        maybe_attributes: None,
                        maybe_default_value: None,
                    },
                ],
                size: fidl::Count(56),
                max_out_of_line: fidl::Count(4294967295),
            },
        ],
        table_declarations: vec![fidl::Table {
            max_handles: Some(fidl::Count(0)),
            maybe_attributes: None,
            name: fidl::CompoundIdentifier("fidl.test.misc/SimpleTable".to_string()),
            location: Some(fidl::Location {
                filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                line: 37,
                column: 6,
            }),
            members: vec![
                fidl::TableMember {
                    ordinal: fidl::Ordinal(1),
                    reserved: false,
                    _type: Some(fidl::Type::Primitive { subtype: fidl::PrimitiveSubtype::Int64 }),
                    name: Some(fidl::Identifier("x".to_string())),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 38,
                        column: 13,
                    }),
                    size: Some(fidl::Count(8)),
                    max_out_of_line: Some(fidl::Count(0)),
                    alignment: Some(fidl::Count(8)),
                    offset: None,
                    maybe_default_value: None,
                },
                fidl::TableMember {
                    ordinal: fidl::Ordinal(2),
                    reserved: true,
                    ..Default::default()
                },
                fidl::TableMember {
                    ordinal: fidl::Ordinal(3),
                    reserved: true,
                    ..Default::default()
                },
                fidl::TableMember {
                    ordinal: fidl::Ordinal(4),
                    reserved: true,
                    ..Default::default()
                },
                fidl::TableMember {
                    ordinal: fidl::Ordinal(5),
                    reserved: false,
                    _type: Some(fidl::Type::Primitive { subtype: fidl::PrimitiveSubtype::Int64 }),
                    name: Some(fidl::Identifier("y".to_string())),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 42,
                        column: 13,
                    }),
                    size: Some(fidl::Count(8)),
                    max_out_of_line: Some(fidl::Count(0)),
                    alignment: Some(fidl::Count(8)),
                    offset: None,
                    maybe_default_value: None,
                },
            ],
            size: fidl::Count(16),
            max_out_of_line: fidl::Count(48),
        }],
        union_declarations: vec![fidl::Union {
            max_handles: Some(fidl::Count(0)),
            maybe_attributes: None,
            name: fidl::CompoundIdentifier("fidl.test.misc/SimpleUnion".to_string()),
            location: Some(fidl::Location {
                filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                line: 29,
                column: 6,
            }),
            members: vec![
                fidl::UnionMember {
                    _type: fidl::Type::Primitive { subtype: fidl::PrimitiveSubtype::Int32 },
                    name: fidl::Identifier("i32".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 30,
                        column: 10,
                    }),
                    size: fidl::Count(4),
                    max_out_of_line: fidl::Count(0),
                    alignment: fidl::Count(4),
                    offset: fidl::Count(8),
                    maybe_attributes: None,
                },
                fidl::UnionMember {
                    _type: fidl::Type::Primitive { subtype: fidl::PrimitiveSubtype::Int64 },
                    name: fidl::Identifier("i64".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 31,
                        column: 10,
                    }),
                    size: fidl::Count(8),
                    max_out_of_line: fidl::Count(0),
                    alignment: fidl::Count(8),
                    offset: fidl::Count(8),
                    maybe_attributes: None,
                },
                fidl::UnionMember {
                    _type: fidl::Type::Identifier {
                        identifier: fidl::CompoundIdentifier(
                            "fidl.test.misc/Int64Struct".to_string(),
                        ),
                        nullable: false,
                    },
                    name: fidl::Identifier("s".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 32,
                        column: 16,
                    }),
                    size: fidl::Count(8),
                    max_out_of_line: fidl::Count(0),
                    alignment: fidl::Count(8),
                    offset: fidl::Count(8),
                    maybe_attributes: None,
                },
                fidl::UnionMember {
                    _type: fidl::Type::Identifier {
                        identifier: fidl::CompoundIdentifier(
                            "fidl.test.misc/Int64Struct".to_string(),
                        ),
                        nullable: true,
                    },
                    name: fidl::Identifier("os".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 33,
                        column: 17,
                    }),
                    size: fidl::Count(8),
                    max_out_of_line: fidl::Count(8),
                    alignment: fidl::Count(8),
                    offset: fidl::Count(8),
                    maybe_attributes: None,
                },
                fidl::UnionMember {
                    _type: fidl::Type::Str { maybe_element_count: None, nullable: false },
                    name: fidl::Identifier("str".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 34,
                        column: 11,
                    }),
                    size: fidl::Count(16),
                    max_out_of_line: fidl::Count(4294967295),
                    alignment: fidl::Count(8),
                    offset: fidl::Count(8),
                    maybe_attributes: None,
                },
            ],
            size: fidl::Count(24),
            max_out_of_line: fidl::Count(4294967295),
            alignment: fidl::Count(8),
        }],
        xunion_declarations: vec![fidl::XUnion {
            max_handles: Some(fidl::Count(0)),
            maybe_attributes: None,
            name: fidl::CompoundIdentifier("fidl.test.misc/SampleXUnion".to_string()),
            location: Some(fidl::Location {
                filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                line: 64,
                column: 7,
            }),
            members: vec![
                fidl::XUnionMember {
                    ordinal: fidl::Ordinal(702498725),
                    _type: fidl::Type::Primitive { subtype: fidl::PrimitiveSubtype::Int32 },
                    name: fidl::Identifier("i".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 65,
                        column: 10,
                    }),
                    size: fidl::Count(4),
                    max_out_of_line: fidl::Count(0),
                    alignment: fidl::Count(4),
                    offset: fidl::Count(0),
                    maybe_attributes: None,
                },
                fidl::XUnionMember {
                    ordinal: fidl::Ordinal(1865512531),
                    _type: fidl::Type::Identifier {
                        identifier: fidl::CompoundIdentifier(
                            "fidl.test.misc/SimpleUnion".to_string(),
                        ),
                        nullable: false,
                    },
                    name: fidl::Identifier("su".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 66,
                        column: 16,
                    }),
                    size: fidl::Count(24),
                    max_out_of_line: fidl::Count(4294967295),
                    alignment: fidl::Count(8),
                    offset: fidl::Count(0),
                    maybe_attributes: None,
                },
                fidl::XUnionMember {
                    ordinal: fidl::Ordinal(811936989),
                    _type: fidl::Type::Identifier {
                        identifier: fidl::CompoundIdentifier(
                            "fidl.test.misc/SimpleTable".to_string(),
                        ),
                        nullable: false,
                    },
                    name: fidl::Identifier("st".to_string()),
                    location: Some(fidl::Location {
                        filename: "../../sdk/lib/fidl/cpp/fidl_test.fidl".to_string(),
                        line: 67,
                        column: 16,
                    }),
                    size: fidl::Count(16),
                    max_out_of_line: fidl::Count(48),
                    alignment: fidl::Count(8),
                    offset: fidl::Count(0),
                    maybe_attributes: None,
                },
            ],
            size: fidl::Count(24),
            max_out_of_line: fidl::Count(4294967295),
            alignment: fidl::Count(8),
        }],
        declaration_order: vec![
            fidl::CompoundIdentifier("fidl.test.misc/Has2OptionalFieldStruct".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/OptionalXUnionInStruct".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/HasOptionalFieldStruct".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/OlderSimpleTable".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/NewerSimpleTable".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/SimpleTable".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/Int64Struct".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/SimpleUnion".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/SampleXUnion".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/InlineXUnionInStruct".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/XUnionInTable".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/Empty".to_string()),
            fidl::CompoundIdentifier("fidl.test.misc/EmptyStructSandwich".to_string()),
        ],
        declarations: fidl::DeclarationsMap(decls),
        library_dependencies: vec![],
    };

    let ir: fidl::Ir = serde_json::from_str(input).unwrap();
    assert_eq!(ir, expected)
}
