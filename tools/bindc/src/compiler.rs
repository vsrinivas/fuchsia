// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bind_library;
use crate::bind_program;
use crate::dependency_graph::{self, DependencyGraph};
use crate::parser_common::{self, CompoundIdentifier, Include};
use std::collections::HashMap;
use std::convert::TryFrom;
use std::ops::Deref;
use std::path::PathBuf;

#[derive(Debug, PartialEq)]
pub enum CompilerError {
    FileOpenError(PathBuf),
    FileReadError(PathBuf),
    FileWriteError(PathBuf),
    BindParserError(parser_common::BindParserError),
    DependencyError(dependency_graph::DependencyError<CompoundIdentifier>),
    DuplicateIdentifier(CompoundIdentifier),
    TypeMismatch(CompoundIdentifier),
    UnresolvedQualification(CompoundIdentifier),
    UndeclaredKey(CompoundIdentifier),
    MissingExtendsKeyword(CompoundIdentifier),
    InvalidExtendsKeyword(CompoundIdentifier),
}

#[allow(dead_code)]
fn resolve_dependencies<'a>(
    program: &bind_program::Ast,
    libraries: impl Iterator<Item = &'a bind_library::Ast> + Clone,
) -> Result<Vec<&'a bind_library::Ast>, CompilerError> {
    (|| {
        let mut graph = DependencyGraph::new();

        for library in libraries.clone() {
            graph.insert_node(library.name.clone(), library);
        }

        for Include { name, .. } in &program.using {
            graph.insert_edge_from_root(name)?;
        }

        for from in libraries {
            for to in &from.using {
                graph.insert_edge(&from.name, &to.name)?;
            }
        }

        graph.resolve()
    })()
    .map_err(CompilerError::DependencyError)
}

#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
enum Symbol {
    DeprecatedKey(u32),
    Key(bind_library::ValueType),
    NumberValue(u64),
    StringValue(String),
    BoolValue(bool),
    EnumValue,
}

impl Symbol {
    #[allow(dead_code)]
    fn to_bytecode(&self) -> u32 {
        // We can only support numeric values until the bytecode representation is changed to handle
        // strings.
        match self {
            Symbol::DeprecatedKey(value) => *value,
            Symbol::NumberValue(value64) => match u32::try_from(*value64) {
                Ok(value32) => value32,
                _ => {
                    unimplemented!("64 bit values are unsupported");
                }
            },
            _ => unimplemented!("Unsupported symbol"),
        }
    }
}

/// Find the namespace of a qualified identifier from the library's includes. Or, if the identifier
/// is unqualified, return the local qualified identifier.
fn find_qualified_identifier(
    declaration: &bind_library::Declaration,
    using: &Vec<parser_common::Include>,
    local_qualified: &CompoundIdentifier,
) -> Result<CompoundIdentifier, CompilerError> {
    if let Some(namespace) = declaration.identifier.parent() {
        // A declaration of a qualified (i.e. non-local) key must be an extension.
        if !declaration.extends {
            return Err(CompilerError::MissingExtendsKeyword(declaration.identifier.clone()));
        }

        // Find the fully qualified name from the included libraries.
        let include = using
            .iter()
            .find(|include| {
                namespace == include.name || Some(namespace.to_string()) == include.alias
            })
            .ok_or(CompilerError::UnresolvedQualification(declaration.identifier.clone()))?;

        return Ok(include.name.nest(declaration.identifier.name.clone()));
    }

    // It is not valid to extend an unqualified (i.e. local) key.
    if declaration.extends {
        return Err(CompilerError::InvalidExtendsKeyword(local_qualified.clone()));
    }

    // An unqualified/local key is scoped to the current library.
    Ok(local_qualified.clone())
}

/// Construct a map of every key and value defined by `libraries`. The identifiers in the symbol
/// table will be fully qualified, i.e. they will contain their full namespace. A symbol is
/// namespaced according to the name of the library it is defined in. If a library defines a value
/// by extending a previously defined key, then that value will be namespaced to the current library
/// and not the library of its key.
#[allow(dead_code)]
fn construct_symbol_table(
    libraries: impl Iterator<Item = impl Deref<Target = bind_library::Ast>>,
) -> Result<HashMap<CompoundIdentifier, Symbol>, CompilerError> {
    let mut symbol_table = HashMap::new();
    for lib in libraries {
        let bind_library::Ast { name, using, declarations } = &*lib;

        for declaration in declarations {
            // Construct a qualified identifier for this key that's namespaced to the current
            // library, discarding any other qualifiers. This identifier is used to scope values
            // defined under this key.
            let local_qualified = name.nest(declaration.identifier.name.clone());

            // Attempt to match the namespace of the key to an include of the current library, or if
            // it is unqualified use the local qualified name. Also do a first pass at checking
            // whether the extend keyword is used correctly.
            let qualified = find_qualified_identifier(declaration, using, &local_qualified)?;

            // Type-check the qualified name against the existing symbols, and check that extended
            // keys are previously defined and that non-extended keys are not.
            match symbol_table.get(&qualified) {
                Some(Symbol::Key(value_type)) => {
                    if !declaration.extends {
                        return Err(CompilerError::DuplicateIdentifier(qualified));
                    }
                    if declaration.value_type != *value_type {
                        return Err(CompilerError::TypeMismatch(qualified));
                    }
                }
                Some(_) => {
                    return Err(CompilerError::TypeMismatch(qualified));
                }
                None => {
                    if declaration.extends {
                        return Err(CompilerError::UndeclaredKey(qualified));
                    }
                    symbol_table.insert(qualified.clone(), Symbol::Key(declaration.value_type));
                }
            }

            // Insert each value associated with the declaration into the symbol table, taking care
            // to scope each identifier under the locally qualified identifier of the key. We don't
            // need to type-check values here since the parser has already done that.
            for value in &declaration.values {
                let qualified_value = local_qualified.nest(value.identifier().to_string());
                if symbol_table.contains_key(&qualified_value) {
                    return Err(CompilerError::DuplicateIdentifier(qualified_value));
                }

                match value {
                    bind_library::Value::Number(_, value) => {
                        symbol_table.insert(qualified_value, Symbol::NumberValue(*value));
                    }
                    bind_library::Value::Str(_, value) => {
                        symbol_table.insert(qualified_value, Symbol::StringValue(value.clone()));
                    }
                    bind_library::Value::Bool(_, value) => {
                        symbol_table.insert(qualified_value, Symbol::BoolValue(*value));
                    }
                    bind_library::Value::Enum(_) => {
                        symbol_table.insert(qualified_value, Symbol::EnumValue);
                    }
                };
            }
        }
    }

    Ok(symbol_table)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;

    mod symbol_table {
        use super::*;

        #[test]
        fn simple_key_and_value() {
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("test"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["symbol"],
                    value_type: bind_library::ValueType::Number,
                    extends: false,
                    values: vec![(bind_library::Value::Number("x".to_string(), 1))],
                }],
            }];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("test", "symbol")),
                Some(&Symbol::Key(bind_library::ValueType::Number))
            );
            assert_eq!(
                st.get(&make_identifier!("test", "symbol", "x")),
                Some(&Symbol::NumberValue(1))
            );
        }

        #[test]
        fn extension() {
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![(bind_library::Value::Number("x".to_string(), 1))],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![Include { name: make_identifier!("lib_a"), alias: None }],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["lib_a", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: true,
                        values: vec![(bind_library::Value::Number("y".to_string(), 2))],
                    }],
                },
            ];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol")),
                Some(&Symbol::Key(bind_library::ValueType::Number))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol", "x")),
                Some(&Symbol::NumberValue(1))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_b", "symbol", "y")),
                Some(&Symbol::NumberValue(2))
            );
        }

        #[test]
        fn aliased_extension() {
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![(bind_library::Value::Number("x".to_string(), 1))],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![Include {
                        name: make_identifier!("lib_a"),
                        alias: Some("alias".to_string()),
                    }],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["alias", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: true,
                        values: vec![(bind_library::Value::Number("y".to_string(), 2))],
                    }],
                },
            ];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol")),
                Some(&Symbol::Key(bind_library::ValueType::Number))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol", "x")),
                Some(&Symbol::NumberValue(1))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_b", "symbol", "y")),
                Some(&Symbol::NumberValue(2))
            );
        }

        #[test]
        fn duplicate_key() {
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("test"),
                using: vec![],
                declarations: vec![
                    bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    },
                    bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    },
                ],
            }];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::DuplicateIdentifier(make_identifier!("test", "symbol")))
            );
        }

        #[test]
        fn duplicate_value() {
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("test"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["symbol"],
                    value_type: bind_library::ValueType::Number,
                    extends: false,
                    values: vec![
                        bind_library::Value::Number("a".to_string(), 1),
                        bind_library::Value::Number("a".to_string(), 2),
                    ],
                }],
            }];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::DuplicateIdentifier(make_identifier!("test", "symbol", "a")))
            );
        }

        #[test]
        fn keys_are_qualified() {
            // The same symbol declared in two libraries should not collide.
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    }],
                },
            ];

            let st = construct_symbol_table(libraries.iter()).unwrap();
            assert_eq!(
                st.get(&make_identifier!("lib_a", "symbol")),
                Some(&Symbol::Key(bind_library::ValueType::Number))
            );
            assert_eq!(
                st.get(&make_identifier!("lib_b", "symbol")),
                Some(&Symbol::Key(bind_library::ValueType::Number))
            );
        }

        #[test]
        fn missing_extend_keyword() {
            // A library referring to a previously declared symbol must use the "extend" keyword.
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["lib_a", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: false,
                        values: vec![],
                    }],
                },
            ];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::MissingExtendsKeyword(make_identifier!("lib_a", "symbol")))
            );
        }

        #[test]
        fn invalid_extend_keyword() {
            // A library cannot declare an unqualified (and therefore locally namespaced) symbol
            // with the "extend" keyword.
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("lib_a"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["symbol"],
                    value_type: bind_library::ValueType::Number,
                    extends: true,
                    values: vec![],
                }],
            }];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::InvalidExtendsKeyword(make_identifier!("lib_a", "symbol")))
            );
        }

        #[test]
        fn unresolved_qualification() {
            // A library cannot refer to a qualified identifier where the qualifier is not in its
            // list of includes.
            let libraries = vec![bind_library::Ast {
                name: make_identifier!("lib_a"),
                using: vec![],
                declarations: vec![bind_library::Declaration {
                    identifier: make_identifier!["lib_b", "symbol"],
                    value_type: bind_library::ValueType::Number,
                    extends: true,
                    values: vec![],
                }],
            }];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::UnresolvedQualification(make_identifier!("lib_b", "symbol")))
            );
        }

        #[test]
        fn undeclared_key() {
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![Include { name: make_identifier!("lib_a"), alias: None }],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["lib_a", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: true,
                        values: vec![],
                    }],
                },
            ];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::UndeclaredKey(make_identifier!("lib_a", "symbol")))
            );
        }

        #[test]
        fn type_mismatch() {
            let libraries = vec![
                bind_library::Ast {
                    name: make_identifier!("lib_a"),
                    using: vec![],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["symbol"],
                        value_type: bind_library::ValueType::Str,
                        extends: false,
                        values: vec![],
                    }],
                },
                bind_library::Ast {
                    name: make_identifier!("lib_b"),
                    using: vec![Include { name: make_identifier!("lib_a"), alias: None }],
                    declarations: vec![bind_library::Declaration {
                        identifier: make_identifier!["lib_a", "symbol"],
                        value_type: bind_library::ValueType::Number,
                        extends: true,
                        values: vec![],
                    }],
                },
            ];

            assert_eq!(
                construct_symbol_table(libraries.iter()),
                Err(CompilerError::TypeMismatch(make_identifier!("lib_a", "symbol")))
            );
        }
    }

    #[test]
    fn dependencies() {
        let program = bind_program::Ast {
            using: vec![Include { name: make_identifier!("A"), alias: None }],
            statements: vec![],
        };
        let libraries = vec![
            bind_library::Ast {
                name: make_identifier!("A"),
                using: vec![Include { name: make_identifier!("A", "B"), alias: None }],
                declarations: vec![],
            },
            bind_library::Ast {
                name: make_identifier!("A", "B"),
                using: vec![],
                declarations: vec![],
            },
            bind_library::Ast {
                name: make_identifier!("A", "C"),
                using: vec![],
                declarations: vec![],
            },
        ];

        assert_eq!(
            resolve_dependencies(&program, libraries.iter()),
            Ok(vec![
                &bind_library::Ast {
                    name: make_identifier!("A"),
                    using: vec![Include { name: make_identifier!("A", "B"), alias: None }],
                    declarations: vec![],
                },
                &bind_library::Ast {
                    name: make_identifier!("A", "B"),
                    using: vec![],
                    declarations: vec![],
                },
            ])
        );
    }

    #[test]
    fn dependencies_error() {
        let program = bind_program::Ast {
            using: vec![Include { name: make_identifier!("A"), alias: None }],
            statements: vec![],
        };
        let libraries = vec![
            bind_library::Ast {
                name: make_identifier!("A"),
                using: vec![Include { name: make_identifier!("A", "B"), alias: None }],
                declarations: vec![],
            },
            bind_library::Ast {
                name: make_identifier!("A", "C"),
                using: vec![],
                declarations: vec![],
            },
        ];

        assert_eq!(
            resolve_dependencies(&program, libraries.iter()),
            Err(CompilerError::DependencyError(
                dependency_graph::DependencyError::MissingDependency(make_identifier!("A", "B"))
            ))
        );
    }
}
