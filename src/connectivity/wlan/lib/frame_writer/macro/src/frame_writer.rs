// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ie::Ie;
use {
    crate::{header::HeaderDefinition, ie::IeDefinition},
    proc_macro::TokenStream,
    quote::quote,
    std::collections::HashMap,
    syn::{
        braced,
        parse::{Parse, ParseStream},
        parse_macro_input,
        punctuated::Punctuated,
        Error, Expr, Ident, Result, Token,
    },
};

macro_rules! unwrap_or_bail {
    ($x:expr) => {
        match $x {
            Err(e) => return TokenStream::from(e.to_compile_error()),
            Ok(x) => x,
        }
    };
}

const GROUP_NAME_HEADERS: &str = "headers";
const GROUP_NAME_IES: &str = "ies";
const GROUP_NAME_PAYLOAD: &str = "payload";

/// A set of `BufferWrite` types which can be written into a buffer.
enum Writeable {
    Header(HeaderDefinition),
    Ie(IeDefinition),
    Payload(Expr),
}

pub trait BufferWrite {
    fn gen_frame_len_tokens(&self) -> Result<proc_macro2::TokenStream>;
    fn gen_write_to_buf_tokens(&self) -> Result<proc_macro2::TokenStream>;
    fn gen_var_declaration_tokens(&self) -> Result<proc_macro2::TokenStream>;
}

impl BufferWrite for Writeable {
    fn gen_frame_len_tokens(&self) -> Result<proc_macro2::TokenStream> {
        match self {
            Writeable::Header(x) => x.gen_frame_len_tokens(),
            Writeable::Ie(x) => x.gen_frame_len_tokens(),
            Writeable::Payload(_) => Ok(quote!(frame_len += payload.len();)),
        }
    }
    fn gen_write_to_buf_tokens(&self) -> Result<proc_macro2::TokenStream> {
        match self {
            Writeable::Header(x) => x.gen_write_to_buf_tokens(),
            Writeable::Ie(x) => x.gen_write_to_buf_tokens(),
            Writeable::Payload(_) => Ok(quote!(w.append_value(&payload[..])?;)),
        }
    }
    fn gen_var_declaration_tokens(&self) -> Result<proc_macro2::TokenStream> {
        match self {
            Writeable::Header(x) => x.gen_var_declaration_tokens(),
            Writeable::Ie(x) => x.gen_var_declaration_tokens(),
            Writeable::Payload(x) => Ok(quote!(let payload = #x;)),
        }
    }
}
struct MacroArgs {
    buffer_source: Expr,
    write_defs: WriteDefinitions,
}

impl Parse for MacroArgs {
    fn parse(input: ParseStream) -> Result<Self> {
        let buffer_source = input.parse::<Expr>()?;
        input.parse::<Token![,]>()?;
        let write_defs = input.parse::<WriteDefinitions>()?;
        Ok(MacroArgs { buffer_source, write_defs })
    }
}

/// A parseable struct representing the macro's arguments.
struct WriteDefinitions(Vec<Writeable>);

impl Parse for WriteDefinitions {
    fn parse(input: ParseStream) -> Result<Self> {
        let content;
        braced!(content in input);
        let groups = Punctuated::<GroupArgs, Token![,]>::parse_terminated(&content)?;

        let mut writeable = vec![];
        for group in groups {
            match group {
                GroupArgs::Headers(data) => {
                    writeable.extend(data.into_iter().map(|x| Writeable::Header(x)));
                }
                GroupArgs::Fields(data) => {
                    writeable.extend(data.into_iter().map(|x| Writeable::Ie(x)));
                }
                GroupArgs::Payload(data) => {
                    writeable.push(Writeable::Payload(data));
                }
            }
        }

        Ok(Self(writeable))
    }
}

/// A parseable struct representing an individual group of definitions such as headers, IEs or
/// the buffer provider.
enum GroupArgs {
    Headers(Vec<HeaderDefinition>),
    Fields(Vec<IeDefinition>),
    Payload(Expr),
}

impl Parse for GroupArgs {
    fn parse(input: ParseStream) -> Result<Self> {
        let name: Ident = input.parse()?;
        input.parse::<Token![:]>()?;

        match name.to_string().as_str() {
            GROUP_NAME_HEADERS => {
                let content;
                braced!(content in input);
                let hdrs: Punctuated<HeaderDefinition, Token![,]> =
                    Punctuated::parse_terminated(&content)?;
                Ok(GroupArgs::Headers(hdrs.into_iter().collect()))
            }
            GROUP_NAME_IES => {
                let content;
                braced!(content in input);
                let ies: Punctuated<IeDefinition, Token![,]> =
                    Punctuated::parse_terminated(&content)?;
                let ies = ies.into_iter().collect::<Vec<_>>();

                // Error if a IE was defined more than once.
                let mut map = HashMap::new();
                for ie in &ies {
                    if let Some(_) = map.insert(ie.type_, ie) {
                        return Err(Error::new(ie.name.span(), "IE defined twice"));
                    }
                }

                let rates_presence =
                    map.iter().fold((false, None), |acc, (type_, ie)| match type_ {
                        Ie::ExtendedRates { .. } => (acc.0, Some(ie)),
                        Ie::Rates => (true, None),
                        _ => acc,
                    });
                if let (false, Some(ie)) = rates_presence {
                    return Err(Error::new(
                        ie.name.span(),
                        "`extended_supported_rates` IE specified without `supported_rates` IE",
                    ));
                }

                Ok(GroupArgs::Fields(ies))
            }
            GROUP_NAME_PAYLOAD => Ok(GroupArgs::Payload(input.parse::<Expr>()?)),
            unknown => Err(Error::new(name.span(), format!("unknown group: '{}'", unknown))),
        }
    }
}

fn process_write_definitions(
    write_defs: Vec<Writeable>,
    make_buf_tokens: proc_macro2::TokenStream,
    return_buf_tokens: proc_macro2::TokenStream,
) -> TokenStream {
    let mut declare_var_tokens = quote!();
    let mut write_to_buf_tokens = quote!();
    let mut frame_len_tokens = quote!(let mut frame_len = 0;);

    for x in write_defs {
        let tokens = unwrap_or_bail!(x.gen_write_to_buf_tokens());
        write_to_buf_tokens = quote!(#write_to_buf_tokens #tokens);

        let tokens = unwrap_or_bail!(x.gen_frame_len_tokens());
        frame_len_tokens = quote!(#frame_len_tokens #tokens);

        let tokens = unwrap_or_bail!(x.gen_var_declaration_tokens());
        declare_var_tokens = quote!(#declare_var_tokens #tokens);
    }

    TokenStream::from(quote! {
        {
            || -> Result<(_, usize), Error> {
                #[allow(unused)]
                use {
                    wlan_common::{
                        appendable::Appendable,
                        buffer_writer::BufferWriter,
                        error::FrameWriteError,
                        ie::{self, IE_PREFIX_LEN, SUPPORTED_RATES_MAX_LEN},
                    },
                    std::convert::AsRef,
                    std::mem::size_of,
                };

                #declare_var_tokens
                #frame_len_tokens

                #make_buf_tokens

                {
                    #write_to_buf_tokens
                }

                #return_buf_tokens
            }()
        }
    })
}

pub fn process_with_buf_provider(input: TokenStream) -> TokenStream {
    let macro_args = parse_macro_input!(input as MacroArgs);
    let buffer_source = macro_args.buffer_source;
    let buf_tokens = quote!(
        let mut buffer_provider = #buffer_source;
        let mut buf = buffer_provider.get_buffer(frame_len)?;
        let mut w = BufferWriter::new(&mut buf[..]);
    );
    let return_buf_tokens = quote!(
        let bytes_written = w.bytes_written();
        Ok((buf, bytes_written))
    );
    process_write_definitions(macro_args.write_defs.0, buf_tokens, return_buf_tokens)
}

pub fn process_with_dynamic_buf(input: TokenStream) -> TokenStream {
    let macro_args = parse_macro_input!(input as MacroArgs);
    let buffer_source = macro_args.buffer_source;
    let buf_tokens = quote!(
        let mut w = #buffer_source;
    );
    let return_buf_tokens = quote!(
        let bytes_written = w.bytes_written();
        Ok((w, bytes_written))
    );
    process_write_definitions(macro_args.write_defs.0, buf_tokens, return_buf_tokens)
}

pub fn process_with_fixed_buf(input: TokenStream) -> TokenStream {
    let macro_args = parse_macro_input!(input as MacroArgs);
    let buffer_source = macro_args.buffer_source;
    let buf_tokens = quote!(
        let mut buf = #buffer_source;
        let mut w = BufferWriter::new(&mut buf[..]);
    );
    let return_buf_tokens = quote!(
        let bytes_written = w.bytes_written();
        Ok((buf, bytes_written))
    );
    process_write_definitions(macro_args.write_defs.0, buf_tokens, return_buf_tokens)
}
