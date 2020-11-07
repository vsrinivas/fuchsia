// cargo-deps: rustc_lexer="0.1"
// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    rustc_lexer::{Token, TokenKind},
    std::{
        borrow::Cow,
        collections::HashSet,
        fs::File,
        io::{self, BufRead, BufReader, BufWriter, Write},
        iter::{self, Iterator, Peekable},
    },
};

fn main() {
    let mut done = HashSet::new();
    for record in io::stdin().lock().lines() {
        // Parse records printed by awk in fix_rust_exhaustive_tables.sh.
        let record = record.unwrap();
        let fields = &mut record.splitn(3, ',');
        let (kind, path, data) = (next(fields), next(fields), next(fields));
        let path_parts = &mut path.splitn(3, ':');
        let path = next(path_parts).strip_prefix("../../").unwrap();
        let (row, col) = (next_int(path_parts), next_int(path_parts));

        // Ensure we never change the same line multiple times, since the first
        // change would invalidate column numbers of the other errors. If there
        // are multiple errors in the same line, they will be dealt with one by
        // one as fix_rust_exhaustive_tables.sh runs this program in a loop.
        if done.contains(&(Cow::Borrowed(path), row)) {
            continue;
        }
        done.insert((Cow::Owned(path.to_owned()), row));

        let mut lines = BufReader::new(File::open(path).unwrap())
            .lines()
            .map(|r| r.unwrap() + "\n")
            .collect::<Vec<_>>();
        match kind {
            "pat" => {
                let fields = &mut data.splitn(2, " | ");
                let (row, suggestion) = (next_int(fields), next(fields));
                let replacement = if suggestion.ends_with("..,") {
                    // https://github.com/rust-lang/rust/issues/78511
                    &suggestion[..suggestion.len() - 1]
                } else {
                    suggestion
                };
                lines[row - 1] = format!("{}\n", replacement);
            }
            "con" => {
                let (i0, j0) = (row - 1, col - 1);
                let mut tokens = tokenize(&lines, (i0, j0)).peekable();
                let typename = parse_path(&mut tokens);
                let block = find_closing_brace(&mut tokens);
                drop(tokens);
                assert_eq!(char_at(&lines, block.close), "}");
                if block.empty {
                    // Change `MyTable {}` to `MyTable::empty()`.
                    assert!(block.inline);
                    assert!(!block.trailing_comma);
                    // Assert both braces are on the same line, so that we only
                    // have to edit one line. This will fail if there is
                    // something like "Table {\n}".
                    let (i, j) = block.close;
                    assert_eq!(i0, i);
                    lines[i] = format!(
                        "{}{}::empty(){}",
                        &lines[i][..j0], // exclude `MyTable {`
                        typename,
                        &lines[i][j + 1..] // exclude `}`
                    );
                } else if block.inline {
                    // Change `stuff }` to `stuff, MyTable::empty() }`.
                    if let Some(pos) = block.pre_close {
                        assert_eq!(char_at(&lines, pos), " ");
                        assert_eq!(pos.0, block.close.0);
                    }
                    let (i, j) = block.pre_close.unwrap_or(block.close);
                    let comma = if block.trailing_comma { "" } else { ", " };
                    lines[i] = format!(
                        "{}{}..{}::empty(){}",
                        &lines[i][..j],
                        comma,
                        typename,
                        &lines[i][j..] // include `}`
                    );
                } else {
                    // Change
                    //
                    //        stuff,
                    //    }
                    //
                    // to
                    //
                    //        stuff,
                    //        ..MyTable::empty()/*INSERT_NEWLINE*/    }
                    //
                    let (i, j) = block.close;
                    // This comma will be badly formatted. We can't 100% rely on
                    // rustfmt because we might be in a macro. So, flag it with
                    // a comment to look at manually.
                    let comma = if block.trailing_comma { "" } else { ",/*FIX_COMMA*/" };
                    let indent = get_indent_width(&lines[i]);
                    assert_eq!(&lines[i][..j], " ".repeat(indent));
                    lines[i] = format!(
                        "{}{}..{}::empty()/*INSERT_NEWLINE*/{}{}",
                        // Use the `}` line plus 4 rather than the previous
                        // line's indentation, since there might be blank
                        // lines coming before.
                        " ".repeat(indent + 4),
                        comma,
                        typename,
                        &lines[i][..j], // indent
                        &lines[i][j..]  // include `}`
                    );
                }
            }
            _ => panic!("invalid kind: {}", kind),
        }
        let mut file = BufWriter::new(File::create(path).unwrap());
        for line in lines {
            file.write(line.as_bytes());
        }
    }
    println!("Fixed {} issue(s).", done.len());
    if done.is_empty() {
        std::process::exit(1);
    }
}

#[track_caller]
fn next<T: Iterator>(it: &mut T) -> T::Item {
    it.next().unwrap()
}

#[track_caller]
fn next_int<'a, T: Iterator<Item = &'a str>>(it: &mut T) -> usize {
    it.next().unwrap().parse::<usize>().unwrap()
}

// Zero-based row and column numbers.
type Pos = (usize, usize);

fn char_at(lines: &[String], (i, j): Pos) -> &str {
    return &lines[i][j..j + 1];
}

fn get_indent_width(line: &str) -> usize {
    line.find(|c: char| !c.is_whitespace()).unwrap()
}

struct TokenInfo {
    token: Token,
    text: String,
    pos: Pos,
}

fn tokenize(lines: &[String], (mut i, mut j): Pos) -> impl Iterator<Item = TokenInfo> + '_ {
    // Combine into one string since some tokens can span lines.
    let content = lines[i..].concat();
    let mut offset = j;
    iter::from_fn(move || {
        if offset >= content.len() {
            return None;
        }
        let token = rustc_lexer::first_token(&content[offset..]);
        let len = token.len;
        let text = &content[offset..offset + len];
        let info = TokenInfo { token, text: text.to_string(), pos: (i, j) };
        let inline_len = std::cmp::min(len, lines[i].len() - j);
        assert_eq!(&text[..inline_len], &lines[i][j..j + inline_len]);
        offset += len;
        for c in text.chars() {
            j += 1;
            if c == '\n' {
                i += 1;
                j = 0;
            }
        }
        Some(info)
    })
}

fn parse_path<T: Iterator<Item = TokenInfo>>(tokens: &mut Peekable<T>) -> String {
    let mut path = String::new();
    loop {
        match tokens.peek() {
            Some(TokenInfo { token: Token { kind, .. }, text, .. })
                if *kind == TokenKind::Ident || *kind == TokenKind::Colon =>
            {
                path.push_str(text);
                tokens.next();
            }
            _ => break,
        }
    }
    path
}

struct Block {
    // Position of the closing brace "}".
    close: Pos,
    // Position of the first character in the span of whitespace coming before
    // the closing brace, if there is any.
    pre_close: Option<Pos>,
    // True if the block is empty (nothing other than whitespace and comments
    // between the open and close braces).
    empty: bool,
    // True if the closing brace is inline with (at least part of) the rest of
    // the expression; that is, it is not on its own line.
    inline: bool,
    // True if there is a trailing comma before the closing brace.
    trailing_comma: bool,
}

fn find_closing_brace<T: Iterator<Item = TokenInfo>>(tokens: &mut T) -> Block {
    for info in tokens.by_ref() {
        if info.token.kind == TokenKind::OpenBrace {
            break;
        }
    }
    let mut depth = 1;
    let mut pre_close = None;
    let mut empty = true;
    let mut inline = true;
    let mut trailing_comma = false;
    for info in tokens.by_ref() {
        depth += match info.token.kind {
            TokenKind::OpenBrace => 1,
            TokenKind::CloseBrace => -1,
            _ => 0,
        };
        if depth == 0 {
            return Block { close: info.pos, pre_close, empty, inline, trailing_comma };
        }
        pre_close = match (pre_close, info.token.kind) {
            (None, TokenKind::Whitespace) => Some(info.pos),
            (Some(pos), TokenKind::Whitespace) => Some(pos),
            _ => None,
        };
        empty = match info.token.kind {
            TokenKind::Whitespace | TokenKind::LineComment | TokenKind::BlockComment { .. } => {
                empty
            }
            _ => false,
        };
        inline = match info.token.kind {
            TokenKind::Whitespace if info.text.contains('\n') => false,
            TokenKind::Whitespace => inline,
            _ => true,
        };
        trailing_comma = match info.token.kind {
            TokenKind::Whitespace | TokenKind::LineComment | TokenKind::BlockComment { .. } => {
                trailing_comma
            }
            TokenKind::Comma => true,
            _ => false,
        };
    }
    panic!("EOF while looking for closing brace");
}
