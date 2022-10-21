// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::ParseError;
use syn::spanned::Spanned;

fn is_matching_attr(name: &str, attr: &syn::Attribute) -> bool {
    attr.path.segments.len() == 1 && attr.path.segments[0].ident == name
}

fn is_command_attr(attr: &syn::Attribute) -> bool {
    is_matching_attr("command", attr)
}

fn is_ffx_attr(attr: &syn::Attribute) -> bool {
    is_matching_attr("ffx", attr)
}

fn is_forces_stdout_logs_path(path: &syn::Path) -> bool {
    path.segments.len() == 1
        && path.segments.first().unwrap().ident == FfxAttrTy::ForcesStdoutLogs.to_string().as_str()
}

#[derive(Clone, Debug)]
pub struct NamedField<'a> {
    pub field_ty: &'a syn::Type,
    pub field_name: &'a syn::Ident,
}

#[derive(Clone, Debug)]
pub enum NamedFieldTy<'a> {
    // No attr, so not interested.
    Blank(NamedField<'a>),
    // Is denoted as #[command].
    Command(NamedField<'a>),
}

impl<'a> NamedFieldTy<'a> {
    pub fn parse(field: &'a syn::Field) -> Result<Self, ParseError> {
        let mut res = Option::<Self>::None;
        let field_name = field.ident.as_ref().expect("field missing ident in struct");
        let field_ty = &field.ty;
        for attr in &field.attrs {
            if is_command_attr(attr) {
                if res.is_some() {
                    return Err(ParseError::DuplicateAttr(attr.span()));
                }
                res.replace(Self::Command(NamedField { field_ty, field_name }));
            }
        }
        Ok(res.unwrap_or(Self::Blank(NamedField { field_ty, field_name })))
    }
}

#[derive(Debug)]
pub enum FfxAttrTy {
    ForcesStdoutLogs,
}

impl ToString for FfxAttrTy {
    fn to_string(&self) -> String {
        match self {
            Self::ForcesStdoutLogs => "forces_stdout_logs".to_owned(),
        }
    }
}

impl FfxAttrTy {
    pub fn parse(attrs: &Vec<syn::Attribute>) -> Result<Option<Self>, ParseError> {
        // This function is really only written from the idea that there's one attribute we care
        // about. This can be refactored later to return a list of attributes rather than just one
        // matching attribute without much work.
        for attr in attrs.iter() {
            if !is_ffx_attr(attr) {
                continue;
            }

            let meta_list =
                match attr.parse_meta().map_err(|_| ParseError::MalformedFfxAttr(attr.span()))? {
                    syn::Meta::List(list) => Ok(list),
                    meta => Err(ParseError::MalformedFfxAttr(meta.span())),
                }?;
            let mut res = None;
            for item in &meta_list.nested {
                let _: Option<Self> = match item {
                    syn::NestedMeta::Meta(syn::Meta::Path(path))
                        if is_forces_stdout_logs_path(path) =>
                    {
                        res.replace(Self::ForcesStdoutLogs)
                            .map(|_| Err(ParseError::DuplicateFfxAttr(path.span())))
                            .transpose()
                    }
                    _ => Err(ParseError::MalformedFfxAttr(item.span())),
                }?;
            }
            let res = res.ok_or(ParseError::MalformedFfxAttr(attr.span()))?;
            return Ok(Some(res));
        }
        Ok(None)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::parse_macro_derive;

    #[test]
    fn test_parse_ffx_attr_ty() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            #[ffx(forces_stdout_logs)]
            struct Foo {}
            "#,
        );
        match FfxAttrTy::parse(&ast.attrs).unwrap() {
            Some(FfxAttrTy::ForcesStdoutLogs) => {}
            e => panic!("Received unexpected parse output: {e:?}"),
        }
    }

    #[test]
    fn test_parse_ffx_attr_ty_failure_duplicate() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            #[ffx(forces_stdout_logs, forces_stdout_logs)]
            struct Foo {}
            "#,
        );
        match FfxAttrTy::parse(&ast.attrs) {
            Ok(r) => panic!("Expected failure. Instead received {r:?}"),
            Err(ParseError::DuplicateFfxAttr(_)) => {}
            e => panic!("Received unexpected error: {e:?}"),
        }
    }

    #[test]
    fn test_parse_ffx_attr_ty_typo() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            #[ffx(force_stdout_loggerooooo)]
            struct Foo {}
            "#,
        );
        match FfxAttrTy::parse(&ast.attrs) {
            Ok(r) => panic!("Expected failure. Instead received {r:?}"),
            Err(ParseError::MalformedFfxAttr(_)) => {}
            e => panic!("Received unexpected error: {e:?}"),
        }
    }

    #[test]
    fn test_parse_ffx_attr_ty_empty() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            #[ffx]
            struct Foo {}
            "#,
        );
        match FfxAttrTy::parse(&ast.attrs) {
            Ok(r) => panic!("Expected failure. Instead received {r:?}"),
            Err(ParseError::MalformedFfxAttr(_)) => {}
            e => panic!("Received unexpected error: {e:?}"),
        }
    }
}
