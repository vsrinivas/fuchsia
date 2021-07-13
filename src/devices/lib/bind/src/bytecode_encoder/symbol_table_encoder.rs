// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bytecode_constants::*;
use crate::bytecode_encoder::error::BindRulesEncodeError;
use std::collections::HashMap;

pub struct SymbolTableEncoder {
    pub encoded_symbols: HashMap<String, u32>,
    unique_key: u32,
    pub bytecode: Vec<u8>,
}

impl SymbolTableEncoder {
    pub fn new() -> Self {
        SymbolTableEncoder {
            encoded_symbols: HashMap::<String, u32>::new(),
            unique_key: SYMB_TBL_START_KEY,
            bytecode: vec![],
        }
    }

    // Assign a unique key to |value| and add it to the list of encoded symbols and
    // the bytecode.
    fn add_symbol(&mut self, value: String) -> Result<u32, BindRulesEncodeError> {
        if value.len() > MAX_STRING_LENGTH {
            return Err(BindRulesEncodeError::InvalidStringLength(value));
        }

        let symbol_key = self.unique_key;
        self.encoded_symbols.insert(value.to_string(), self.unique_key);

        // Add the symbol to the bytecode. The string value is followed by a zero
        // terminator.
        self.bytecode.extend_from_slice(&self.unique_key.to_le_bytes());
        self.bytecode.append(&mut value.into_bytes());
        self.bytecode.push(0);

        self.unique_key += 1;
        Ok(symbol_key)
    }

    // Retrieve the key for |value|. Add |value| if it's missing.
    pub fn get_key(&mut self, value: String) -> Result<u32, BindRulesEncodeError> {
        match self.encoded_symbols.get(&value) {
            Some(key) => Ok(*key),
            None => self.add_symbol(value),
        }
    }
}
