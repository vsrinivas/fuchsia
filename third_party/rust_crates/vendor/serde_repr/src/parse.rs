use proc_macro2::Span;
use syn::parse::{Error, Parse, ParseStream, Parser, Result};
use syn::{parenthesized, Data, DeriveInput, Fields, Ident};

pub struct Input {
    pub ident: Ident,
    pub repr: Ident,
    pub variants: Vec<Variant>,
}

pub struct Variant {
    pub ident: Ident,
}

impl Parse for Input {
    fn parse(input: ParseStream) -> Result<Self> {
        let call_site = Span::call_site();
        let derive_input = DeriveInput::parse(input)?;

        let data = match derive_input.data {
            Data::Enum(data) => data,
            _ => {
                return Err(Error::new(call_site, "input must be an enum"));
            }
        };

        let variants = data
            .variants
            .into_iter()
            .map(|variant| match variant.fields {
                Fields::Unit => Ok(Variant {
                    ident: variant.ident,
                }),
                Fields::Named(_) | Fields::Unnamed(_) => {
                    Err(Error::new(variant.ident.span(), "must be a unit variant"))
                }
            })
            .collect::<Result<Vec<Variant>>>()?;

        if variants.is_empty() {
            return Err(Error::new(call_site, "there must be at least one variant"));
        }

        let generics = derive_input.generics;
        if !generics.params.is_empty() || generics.where_clause.is_some() {
            return Err(Error::new(call_site, "generic enum is not supported"));
        }

        let mut repr = None;
        for attr in derive_input.attrs {
            if attr.path.is_ident("repr") {
                fn repr_arg(input: ParseStream) -> Result<Ident> {
                    let content;
                    parenthesized!(content in input);
                    content.parse()
                }
                let ty = repr_arg.parse2(attr.tts)?;
                repr = Some(ty);
                break;
            }
        }
        let repr = repr.ok_or_else(|| Error::new(call_site, "missing #[repr(...)] attribute"))?;

        Ok(Input {
            ident: derive_input.ident,
            repr,
            variants,
        })
    }
}
