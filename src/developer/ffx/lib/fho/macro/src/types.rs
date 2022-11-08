// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;

use crate::errors::ParseError;
use syn::{spanned::Spanned, ExprCall, NestedMeta};

fn is_matching_attr(name: &str, attr: &syn::Attribute) -> bool {
    attr.path.segments.len() == 1 && attr.path.segments[0].ident == name
}

fn is_command_attr(attr: &syn::Attribute) -> bool {
    is_matching_attr("command", attr)
}

fn is_ffx_attr(attr: &syn::Attribute) -> bool {
    is_matching_attr("ffx", attr)
}

fn is_check_attr(attr: &syn::Attribute) -> bool {
    is_matching_attr("check", attr)
}

fn is_forces_stdout_logs_path(path: &syn::Path) -> bool {
    path.segments.len() == 1
        && path.segments.first().unwrap().ident == FfxFlag::ForcesStdoutLogs.to_string().as_str()
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

#[derive(Debug, PartialEq, Eq, Hash)]
pub enum FfxFlag {
    ForcesStdoutLogs,
}

impl ToString for FfxFlag {
    fn to_string(&self) -> String {
        match self {
            Self::ForcesStdoutLogs => "forces_stdout_logs".to_owned(),
        }
    }
}

impl TryFrom<&NestedMeta> for FfxFlag {
    type Error = ParseError;
    fn try_from(value: &NestedMeta) -> Result<Self, Self::Error> {
        match value {
            syn::NestedMeta::Meta(syn::Meta::Path(path)) if is_forces_stdout_logs_path(path) => {
                Ok(Self::ForcesStdoutLogs)
            }
            _ => Err(ParseError::MalformedFfxAttr(value.span())),
        }
    }
}

#[derive(Debug)]
pub struct FromEnvAttributes {
    pub flags: HashSet<FfxFlag>,
    pub checks: Vec<ExprCall>,
}

impl FromEnvAttributes {
    pub fn from_attrs(attrs: &Vec<syn::Attribute>) -> Result<Self, ParseError> {
        let mut flags = HashSet::new();
        let mut checks = Vec::new();
        for attr in attrs.iter() {
            if is_ffx_attr(attr) {
                let meta_list = match attr
                    .parse_meta()
                    .map_err(|_| ParseError::MalformedFfxAttr(attr.span()))?
                {
                    syn::Meta::List(list) => Ok(list),
                    meta => Err(ParseError::MalformedFfxAttr(meta.span())),
                }?;
                for item in &meta_list.nested {
                    let flag = item.try_into()?;
                    if !flags.insert(flag) {
                        return Err(ParseError::DuplicateFfxAttr(item.span()));
                    }
                }
            } else if is_check_attr(attr) {
                checks
                    .push(attr.parse_args().map_err(|_| ParseError::InvalidCheckAttr(attr.span()))?)
            }
        }
        Ok(Self { flags, checks })
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
        assert!(
            FromEnvAttributes::from_attrs(&ast.attrs)
                .unwrap()
                .flags
                .contains(&FfxFlag::ForcesStdoutLogs),
            "Expected forces_stdout_logs attribute"
        );
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
        match FromEnvAttributes::from_attrs(&ast.attrs) {
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
        match FromEnvAttributes::from_attrs(&ast.attrs) {
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
        match FromEnvAttributes::from_attrs(&ast.attrs) {
            Ok(r) => panic!("Expected failure. Instead received {r:?}"),
            Err(ParseError::MalformedFfxAttr(_)) => {}
            e => panic!("Received unexpected error: {e:?}"),
        }
    }

    #[test]
    fn test_parse_ffx_attr_ty_check() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            #[check(ThingamaBobber("with-a-string"))]
            struct Foo {}
            "#,
        );
        let checks = FromEnvAttributes::from_attrs(&ast.attrs).unwrap().checks;
        assert_eq!(checks.len(), 1, "Expected a check attribute");
    }

    #[test]
    fn test_parse_ffx_attr_ty_check_invalid() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            #[check = ThingamaBobber("with-a-string")]
            struct Foo {}
            "#,
        );
        assert!(
            matches!(
                FromEnvAttributes::from_attrs(&ast.attrs),
                Err(ParseError::InvalidCheckAttr(_))
            ),
            "Expected error parsing invalid check"
        );
    }

    #[test]
    fn test_parse_ffx_attr_ty_check_empty() {
        let ast = parse_macro_derive(
            r#"
            #[derive(FfxTool)]
            #[check]
            struct Foo {}
            "#,
        );
        assert!(
            matches!(
                FromEnvAttributes::from_attrs(&ast.attrs),
                Err(ParseError::InvalidCheckAttr(_))
            ),
            "Expected error parsing invalid check"
        );
    }
}
