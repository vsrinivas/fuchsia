// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod compiler;
pub mod dependency_graph;
pub mod instruction;
pub mod symbol_table;

pub use self::compiler::{
    compile, compile_bind, compile_statements, BindRules, BindRulesDecodeError, CompiledBindRules,
    CompilerError, CompositeBindRules, SymbolicInstruction, SymbolicInstructionInfo,
};

pub use self::symbol_table::{get_deprecated_key_identifiers, Symbol, SymbolTable};
