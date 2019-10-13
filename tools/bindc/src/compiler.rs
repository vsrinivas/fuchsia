// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bind_library;
use crate::bind_program;
use crate::dependency_graph::{self, DependencyGraph};
use crate::parser_common::{self, CompoundIdentifier, Include};
use std::path::PathBuf;

#[derive(Debug, PartialEq)]
pub enum CompilerError {
    FileOpenError(PathBuf),
    FileReadError(PathBuf),
    FileWriteError(PathBuf),
    BindParserError(parser_common::BindParserError),
    DependencyError(dependency_graph::DependencyError<CompoundIdentifier>),
    UnknownKey(CompoundIdentifier),
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

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;

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
