// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod compiler;
pub mod dependency_graph;
pub mod instruction;

pub use self::compiler::{
    compile, compile_statements, get_deprecated_key_identifiers, BindRules, BindRulesDecodeError,
    CompilerError, CompositeBindRules, Symbol, SymbolTable, SymbolicInstruction,
    SymbolicInstructionInfo,
};
