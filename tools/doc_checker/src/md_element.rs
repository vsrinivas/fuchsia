// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! md_parser defines the traits and structs used to parse markdown files into elements that can be
//! checked.

use crate::{parser, DocLine};
pub use pulldown_cmark::{CowStr, LinkType, Options, Parser, Tag};
use std::{fmt::Debug, path::PathBuf};

/// Element is a high level construct which collects the low level Tag objects into
/// a single element. This removes the need for checks to deal with stateful processing of the event stream.
#[derive(Debug, PartialEq)]
pub enum Element<'a> {
    /// A generic block of elements. The tuple is (block_type, elements, doc_line).
    Block(Tag<'a>, Vec<Element<'a>>, DocLine),
    /// An inline `code` string.
    Code(CowStr<'a>, DocLine),
    /// A ``` code fence
    CodeBlock(CowStr<'a>, Vec<Element<'a>>, DocLine),
    /// A footnote reference
    /// TODO: I have not seen one of these in our docs.
    FootnoteReference(CowStr<'a>, DocLine),
    /// Hard break newline.
    HardBreak(DocLine),
    /// HTML
    Html(CowStr<'a>, DocLine),
    /// Image block. (link_type, image_url, title, elements, doc_line)
    Image(LinkType, CowStr<'a>, CowStr<'a>, Vec<Element<'a>>, DocLine),
    /// Link block.  (link_type, link_url, title, elements, doc_line)
    Link(LinkType, CowStr<'a>, CowStr<'a>, Vec<Element<'a>>, DocLine),
    /// List (starting number or None, items, doc_line)
    List(Option<u64>, Vec<Element<'a>>, DocLine),
    /// Softbreak newline.
    SoftBreak(DocLine),
    //HR Rule
    Rule(DocLine),
    /// TaskList. bool indicating checked.
    TaskListMarker(bool, DocLine),
    /// Text
    Text(CowStr<'a>, DocLine),
}

#[allow(dead_code)]
impl<'a> Element<'a> {
    pub fn doc_line(&self) -> DocLine {
        let doc_line = match self {
            Element::Block(_, _, doc_line) => doc_line,
            Element::Code(_, doc_line) => doc_line,
            Element::CodeBlock(_, _, doc_line) => doc_line,
            Element::FootnoteReference(_, doc_line) => doc_line,
            Element::HardBreak(doc_line) => doc_line,
            Element::Html(_, doc_line) => doc_line,
            Element::Image(_, _, _, _, doc_line) => doc_line,
            Element::Link(_, _, _, _, doc_line) => doc_line,
            Element::List(_, _, doc_line) => doc_line,
            Element::Rule(doc_line) => doc_line,
            Element::SoftBreak(doc_line) => doc_line,
            Element::TaskListMarker(_, doc_line) => doc_line,
            Element::Text(_, doc_line) => doc_line,
        };
        doc_line.clone()
    }

    pub fn get_contents(&self) -> String {
        match self {
            Element::Block(_, elements, _) => {
                elements.iter().map(|e| e.get_contents()).collect::<Vec<String>>().join(" ")
            }
            Element::Code(code, _) => code.to_string(),
            Element::CodeBlock(code, elements, _) => {
                let mut parts = vec![code.to_string()];
                parts.extend(elements.iter().map(|e| e.get_contents()));
                parts.join(" ")
            }
            Element::FootnoteReference(footnote, _) => footnote.to_string(),
            Element::HardBreak(_) => "\n".to_string(),
            Element::Html(html, _) => html.to_string(),
            Element::Image(_, _, title, elements, _) => {
                let mut parts = vec![title.to_string()];
                parts.extend(elements.iter().map(|e| e.get_contents()));
                parts.join(" ")
            }
            Element::Link(_, _, title, elements, _) => {
                let mut parts = vec![title.to_string()];
                parts.extend(elements.iter().map(|e| e.get_contents()));
                parts.join(" ")
            }
            Element::List(_, items, _) => {
                items.iter().map(|e| e.get_contents()).collect::<Vec<String>>().join(" ")
            }
            Element::Rule(_) => "".to_string(),
            Element::SoftBreak(_) => " ".to_string(),
            Element::TaskListMarker(_, _) => " ".to_string(),
            Element::Text(text, _) => text.to_string(),
        }
    }

    pub fn get_links(&self) -> Option<Vec<&Element<'a>>> {
        match self {
            Element::Block(_, elements, _) => {
                let links = elements
                    .iter()
                    .filter(|e| {
                        matches!(e, Element::Link(_, _, _, _, _) | Element::Image(_, _, _, _, _))
                    })
                    .collect();
                Some(links)
            }
            Element::Code(_, _) => None,
            Element::CodeBlock(_, elements, _) => {
                let links = elements
                    .iter()
                    .filter(|e| {
                        matches!(e, Element::Link(_, _, _, _, _) | Element::Image(_, _, _, _, _))
                    })
                    .collect();
                Some(links)
            }
            Element::FootnoteReference(_, _) => None,
            Element::HardBreak(_) => None,
            Element::Html(_, _) => None,
            Element::Image(_, _, _, elements, _) => {
                let mut links: Vec<&Element<'_>> = elements
                    .iter()
                    .filter(|e| {
                        matches!(e, Element::Link(_, _, _, _, _) | Element::Image(_, _, _, _, _))
                    })
                    .collect();
                links.push(self);
                Some(links)
            }
            Element::Link(_, _, _, _, _) => Some(vec![self]),
            Element::List(_, elements, _) => {
                let links = elements
                    .iter()
                    .filter(|e| {
                        matches!(e, Element::Link(_, _, _, _, _) | Element::Image(_, _, _, _, _))
                    })
                    .collect();
                Some(links)
            }
            Element::Rule(_) => None,
            Element::SoftBreak(_) => None,
            Element::TaskListMarker(_, _) => None,
            Element::Text(_, _) => None,
        }
    }
}

pub struct DocContext<'a> {
    pub file_name: PathBuf,
    pub line_num: usize,
    pub(crate) parser: pulldown_cmark::Parser<'a>,
}

impl<'a> DocContext<'a> {
    pub fn new(filename: PathBuf, text: &'a str) -> DocContext<'a> {
        let options = Options::empty();
        DocContext { file_name: filename, line_num: 1, parser: Parser::new_ext(text, options) }
    }

    pub fn line(&self) -> DocLine {
        DocLine { line_num: self.line_num, file_name: self.file_name.clone() }
    }
}

impl<'a> Iterator for DocContext<'a> {
    type Item = Element<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        self.parser.next().map(|event| parser::element_from_event(event, self))
    }
}
