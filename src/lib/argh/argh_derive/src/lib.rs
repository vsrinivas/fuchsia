// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

/// Implementation of the `FromArgs` and `argh(...)` derive attributes.
///
/// For more thorough documentation, see the `argh` crate itself.

extern crate argh_shared_for_host as argh_shared;
extern crate proc_macro;

use {
    crate::{
        errors::Errors,
        parse_attrs::{
            FieldAttrs,
            FieldKind,
            TypeAttrs,
        },
    },
    proc_macro2::{Span, TokenStream},
    quote::{ToTokens, quote, quote_spanned},
    std::str::FromStr,
};

mod errors;
mod help;
mod parse_attrs;

/// Entrypoint for `#[derive(FromArgs)]`.
#[proc_macro_derive(FromArgs, attributes(argh))]
pub fn argh_derive(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    let ast = syn::parse_macro_input!(input as syn::DeriveInput);
    let gen = impl_from_args(&ast);
    gen.into()
}

/// Transform the input into a token stream containing any generated implementations,
/// as well as all errors that occurred.
fn impl_from_args(input: &syn::DeriveInput) -> TokenStream {
    let errors = &Errors::default();
    if input.generics.params.len() != 0 {
        errors.err(
            &input.generics,
            "`#![derive(FromArgs)]` cannot be applied to types with generic parameters",
        );
    }
    let type_attrs = &TypeAttrs::parse(errors, input);
    let mut output_tokens = match &input.data {
        syn::Data::Struct(ds) => impl_from_args_struct(errors, &input.ident, type_attrs, ds),
        syn::Data::Enum(de) => impl_from_args_enum(errors, &input.ident, type_attrs, de),
        syn::Data::Union(_) => {
            errors.err(input, "`#[derive(FromArgs)]` cannot be applied to unions");
            TokenStream::new()
        }
    };
    errors.to_tokens(&mut output_tokens);
    output_tokens
}

/// The kind of optionality a parameter has.
enum Optionality {
    None,
    Defaulted(TokenStream),
    Optional,
}

impl Optionality {
    /// Whether or not this is `Optionality::None`
    fn is_required(&self) -> bool {
        if let Optionality::None = self {
            true
        } else {
            false
        }
    }
}

/// A field of a `#![derive(FromArgs)]` struct with attributes and some other
/// notable metadata appended.
struct StructField<'a> {
    /// The original parsed field
    field: &'a syn::Field,
    /// The parsed attributes of the field
    attrs: FieldAttrs,
    /// The field name. This is contained optionally inside `field`,
    /// but is duplicated non-optionally here to indicate that all field that
    /// have reached this point must have a field name, and it no longer
    /// needs to be unwrapped.
    name: &'a syn::Ident,
    /// Similar to `name` above, this is contained optionally inside `FieldAttrs`,
    /// but here is fully present to indicate that we only have to consider fields
    /// with a valid `kind` at this point.
    kind: FieldKind,
    // If `field.ty` is `Vec<T>` or `Option<T>`, this is `T`, otherwise it's `&field.ty`.
    // This is used to enable consistent parsing code between optional and non-optional
    // keyed and subcommand fields.
    ty_without_wrapper: &'a syn::Type,
    // Whether the field represents an optional value, such as an `Option` subcommand field
    // or an `Option` or `Vec` keyed argument, or if it has a `default`.
    optionality: Optionality,
    // The `--`-prefixed name of the option, if one exists.
    long_name: Option<String>,
}

impl<'a> StructField<'a> {
    /// Attempts to parse a field of a `#[derive(FromArgs)]` struct, pulling out the
    /// fields required for code generation.
    fn new(errors: &Errors, field: &'a syn::Field, attrs: FieldAttrs) -> Option<Self> {
        let name = field.ident.as_ref().expect("missing ident for named field");

        // Ensure that one "kind" is present (switch, option, subcommand)
        let kind = if let Some(field_type) = &attrs.field_type {
            field_type.kind
        } else {
            errors.err(
                field,
                concat!(
                    "Missing `argh` field kind attribute.\n",
                    "Expected one of: `switch`, `option`, `subcommand",
                ),
            );
            return None
        };

        // Parse out whether a field is optional (`Option` or `Vec`).
        let optionality;
        let ty_without_wrapper;
        match kind {
            FieldKind::Switch => {
                if !ty_expect_switch(errors, &field.ty) {
                    return None
                }
                optionality = Optionality::Optional;
                ty_without_wrapper = &field.ty;
            },
            FieldKind::Option => {
                if let Some(default) = &attrs.default {
                    let tokens = match TokenStream::from_str(&default.value()) {
                        Ok(tokens) => tokens,
                        Err(_) => {
                            errors.err(&default, "Invalid tokens: unable to lex `default` value");
                            return None
                        }
                    };
                    // Set the span of the generated tokens to the string literal
                    let tokens: TokenStream = tokens.into_iter()
                        .map(|mut tree| {
                            tree.set_span(default.span().clone());
                            tree
                        })
                        .collect();
                    optionality = Optionality::Defaulted(tokens);
                    ty_without_wrapper = &field.ty;
                } else {
                    let inner = ty_inner(&["Option", "Vec"], &field.ty);
                    optionality = if inner.is_some() {
                        Optionality::Optional
                    } else {
                        Optionality::None
                    };
                    ty_without_wrapper = inner.unwrap_or(&field.ty);
                }
            },
            FieldKind::SubCommand => {
                let inner = ty_inner(&["Option"], &field.ty);
                optionality = if inner.is_some() {
                    Optionality::Optional
                } else {
                    Optionality::None
                };
                ty_without_wrapper = inner.unwrap_or(&field.ty);
            }
        }

        // Determine the "long" name of options and switches.
        // Defaults to the kebab-case'd field name if `#[argh(long = "...")]` is omitted.
        let long_name = match kind {
            FieldKind::Switch | FieldKind::Option => {
                let long_name = attrs.long.as_ref().map(syn::LitStr::value).unwrap_or_else(|| {
                    heck::KebabCase::to_kebab_case(&*name.to_string())
                });
                if long_name == "help" {
                    errors.err(field, "Custom `--help` flags are not supported.");
                }
                let long_name = format!("--{}", long_name);
                Some(long_name)
            }
            FieldKind::SubCommand => None,
        };

        Some(StructField {
            field,
            attrs,
            kind,
            optionality,
            ty_without_wrapper,
            name,
            long_name,
        })
    }
}

/// Implements `FromArgs` and `TopLevelCommand` or `SubCommand` for a `#[derive(FromArgs)]` struct.
fn impl_from_args_struct(
    errors: &Errors,
    name: &syn::Ident,
    type_attrs: &TypeAttrs,
    ds: &syn::DataStruct,
) -> TokenStream {
    let fields = match &ds.fields {
        syn::Fields::Named(fields) => fields,
        syn::Fields::Unnamed(_) => {
            errors.err(&ds.struct_token, "`#![derive(FromArgs)]` is not currently supported on tuple structs");
            return TokenStream::new();
        }
        syn::Fields::Unit => {
            errors.err(&ds.struct_token, "#![derive(FromArgs)]` cannot be applied to unit structs");
            return TokenStream::new();
        }
    };

    let fields: Vec<_> = fields.named.iter().filter_map(|field| {
        let attrs = FieldAttrs::parse(errors, field);
        StructField::new(errors, field, attrs)
    }).collect();

    let top_or_sub_cmd_impl = top_or_sub_cmd_impl(errors, name, type_attrs);
    let init_fields = declare_local_storage_for_fields(&fields);
    let unwrap_fields = unwrap_fields(&fields);

    let flag_output_table = fields.iter().filter_map(|field| {
        let field_name = &field.field.ident;
        match field.kind {
            FieldKind::Option => Some(quote! { argh::CmdOption::Value(&mut #field_name) }),
            FieldKind::SubCommand => None,
            FieldKind::Switch => Some(quote! { argh::CmdOption::Flag(&mut #field_name) }),
        }
    });

    let flag_str_to_output_table_map = flag_str_to_output_table_map_entries(&fields);

    let mut subcommands_iter = fields.iter()
        .filter(|field| field.kind == FieldKind::SubCommand)
        .fuse();

    let subcommand: Option<&StructField<'_>> = subcommands_iter.next();
    while let Some(dup_subcommand) = subcommands_iter.next() {
        errors.duplicate_attrs("subcommand", subcommand.unwrap().field, dup_subcommand.field);
    }

    let impl_span = Span::call_site();

    let missing_requirements_ident = syn::Ident::new("__missing_requirements", impl_span.clone());

    let append_missing_requirements = append_missing_requirements(
        &missing_requirements_ident,
        &fields,
    );

    let check_subcommands = if let Some(subcommand) = subcommand {
        let name = subcommand.name;
        let ty = subcommand.ty_without_wrapper;
        quote_spanned! { impl_span =>
            for __subcommand in <#ty as argh::SubCommands>::COMMANDS {
                if __subcommand.name == __next_arg {
                    let mut __command = __cmd_name.to_owned();
                    __command.push(__subcommand.name);
                    let __prepended_help;
                    let __remaining_args = if __help {
                        __prepended_help = argh::prepend_help(__remaining_args);
                        &__prepended_help
                    } else {
                        __remaining_args
                    };
                    #name = Some(<#ty as argh::FromArgs>::from_args(&__command, __remaining_args)?);
                    // Unset `help`, since we handled it in the subcommand
                    __help = false;
                    break 'parse_args;
                }
            }
        }
    } else {
        TokenStream::new()
    };

    // Identifier referring to a value containing the name of the current command as an `&[&str]`.
    let cmd_name_str_array_ident = syn::Ident::new("__cmd_name", impl_span.clone());
    let help = help::help(errors, cmd_name_str_array_ident, type_attrs, &fields, subcommand);

    let trait_impl = quote_spanned! { impl_span =>
        impl argh::FromArgs for #name {
            fn from_args(__cmd_name: &[&str], __args: &[&str])
                -> std::result::Result<Self, argh::EarlyExit>
            {
                #( #init_fields )*
                let __flag_output_table = &mut [
                    #( #flag_output_table, )*
                ];

                let mut __help = false;
                let mut __remaining_args = __args;
                'parse_args: while let Some(&__next_arg) = __remaining_args.get(0) {
                    __remaining_args = &__remaining_args[1..];
                    if __next_arg == "--help" || __next_arg == "help" {
                        __help = true;
                        continue;
                    }

                    if __next_arg.starts_with("-") {
                        if __help {
                            return Err(
                                "Trailing arguments are not allowed after `help`."
                                    .to_string()
                                    .into()
                            );
                        }

                        argh::parse_option(
                            __next_arg,
                            &mut __remaining_args,
                            __flag_output_table,
                            &[ #( #flag_str_to_output_table_map ,)* ],
                        )?;
                        continue;
                    }

                    #check_subcommands
                }

                if __help {
                    return std::result::Result::Err(argh::EarlyExit {
                        output: #help,
                        status: std::result::Result::Ok(()),
                    });
                }

                let mut #missing_requirements_ident = argh::MissingRequirements::default();
                #(
                    #append_missing_requirements
                )*
                #missing_requirements_ident.err_on_any()?;

                Ok(Self {
                    #( #unwrap_fields, )*
                })
            }
        }

        #top_or_sub_cmd_impl
    };

    trait_impl.into()
}

/// Implement `argh::TopLevelCommand` or `argh::SubCommand` as appropriate.
fn top_or_sub_cmd_impl(errors: &Errors, name: &syn::Ident, type_attrs: &TypeAttrs) -> TokenStream {
    let description = help::require_description(
        errors,
        name.span(),
        &type_attrs.description,
        "type",
    );
    if type_attrs.is_subcommand.is_none() {
        // Not a subcommand
        quote! {
            impl argh::TopLevelCommand for #name {}
        }
    } else {
        let empty_str = syn::LitStr::new("", Span::call_site());
        let subcommand_name = type_attrs.name.as_ref().unwrap_or_else(|| {
            errors.err(name, "`#[argh(name = \"...\")]` attribute is required for subcommands");
            &empty_str
        });
        quote! {
            impl argh::SubCommand for #name {
                const COMMAND: &'static argh::CommandInfo = &argh::CommandInfo {
                    name: #subcommand_name,
                    description: #description,
                };
            }
        }
    }
}

/// Declare a local slots to store each field in during parsing.
///
/// Most fields are stored in `Option<FieldType>` locals.
/// `argh(option)` fields are stored in a `ParseValueSlotTy` along with a
/// function that knows how to decode the appropriate value.
fn declare_local_storage_for_fields<'a>(fields: &'a [StructField<'a>]) -> impl Iterator<Item = TokenStream> + 'a {
    fields.iter().map(|field| {
        let field_name = &field.field.ident;
        let field_type = &field.ty_without_wrapper;

        // Wrap field types in `Option` if they aren't already `Option` or `Vec`-wrapped.
        let field_slot_type = match field.optionality {
            Optionality::Optional => (&field.field.ty).into_token_stream(),
            Optionality::None | Optionality::Defaulted(_) => {
                quote! { std::option::Option<#field_type> }
            }
        };

        match field.kind {
            FieldKind::Option => {
                let from_str_fn = match &field.attrs.from_str_fn {
                    Some(from_str_fn) => from_str_fn.into_token_stream(),
                    None => {
                        quote! {
                            <#field_type as argh::FromArgValue>::from_arg_value
                        }
                    }
                };

                quote! {
                    let mut #field_name: argh::ParseValueSlotTy<#field_slot_type, #field_type>
                        = argh::ParseValueSlotTy {
                            slot: std::default::Default::default(),
                            parse_func: #from_str_fn,
                        };
                }
            },
            FieldKind::SubCommand => {
                quote! { let mut #field_name: #field_slot_type = None; }
            },
            FieldKind::Switch => {
                quote! { let mut #field_name: #field_slot_type = argh::Flag::default(); }
            },
        }
    })
}

/// Unwrap non-optional fields and take options out of their tuple slots.
fn unwrap_fields<'a>(fields: &'a [StructField<'a>]) -> impl Iterator<Item = TokenStream> + 'a {
    fields.iter().map(|field| {
        let field_name = field.name;
        match field.kind {
            FieldKind::Option => {
                match &field.optionality {
                    Optionality::None => quote! { #field_name: #field_name.slot.unwrap() },
                    Optionality::Optional => quote! { #field_name: #field_name.slot },
                    Optionality::Defaulted(tokens) => {
                        quote! {
                            #field_name: #field_name.slot.unwrap_or_else(|| #tokens)
                        }
                    }
                }
            }
            FieldKind::Switch => field_name.into_token_stream(),
            FieldKind::SubCommand => {
                match field.optionality {
                    Optionality::None => quote! { #field_name: #field_name.unwrap() },
                    Optionality::Optional => field_name.into_token_stream(),
                    Optionality::Defaulted(_) => unreachable!(),
                }
            }
        }
    })
}

/// Entries of tokens like `("--some-flag-key", 5)` that map from a flag key string
/// to an index in the output table.
fn flag_str_to_output_table_map_entries<'a>(fields: &'a [StructField<'a>]) -> Vec<TokenStream> {
    let mut flag_str_to_output_table_map = vec![];
    for (i, (field, long_name)) in fields.iter()
        .filter_map(|field| field.long_name.as_ref().map(|long_name| (field, long_name)))
        .enumerate()
    {
        if let Some(short) = &field.attrs.short {
            let short = format!("-{}", short.value());
            flag_str_to_output_table_map.push(quote! { (#short, #i) });
        }

        flag_str_to_output_table_map.push(quote! { (#long_name, #i) });
    }
    flag_str_to_output_table_map
}

/// For each non-optional field, add an entry to the `argh::MissingRequirements`.
fn append_missing_requirements<'a>(
    // missing_requirements_ident
    mri: &syn::Ident,
    fields: &'a [StructField<'a>],
) -> impl Iterator<Item = TokenStream> + 'a {
    let mri = mri.clone();
    fields.iter().filter(|f| f.optionality.is_required()).map(move |field| {
        let field_name = field.name;
        match field.kind {
            FieldKind::Switch => unreachable!("switches are always optional"),
            FieldKind::Option => {
                let name = field.long_name.as_ref().expect("options always have a long name");
                quote! {
                    if #field_name.slot.is_none() {
                        #mri.missing_option(#name)
                    }
                }
            }
            FieldKind::SubCommand => {
                let ty = field.ty_without_wrapper;
                quote! {
                    if #field_name.is_none() {
                        #mri.missing_subcommands(
                            <#ty as argh::SubCommands>::COMMANDS,
                        )
                    }
                }
            }
        }
    })
}

/// Require that a type can be a `switch`.
/// Throws an error for all types except booleans and integers
fn ty_expect_switch(errors: &Errors, ty: &syn::Type) -> bool {
    fn ty_can_be_switch(ty: &syn::Type) -> bool {
        if let syn::Type::Path(path) = ty {
            if path.qself.is_some() { return false }
            if path.path.segments.len() != 1 { return false }
            let ident = &path.path.segments[0].ident;
            [
                "bool",
                "u8",
                "u16",
                "u32",
                "u64",
                "u128",
                "i8",
                "i16",
                "i32",
                "i64",
                "i128",
            ].iter().any(|path| ident == path)
        } else {
            false
        }
    }

    let res = ty_can_be_switch(ty);
    if !res {
        errors.err(ty, "switches must be of type `bool` or integer type");
    }
    res
}


/// Returns `Some(T)` if a type is `wrapper_name<T>` for any `wrapper_name` in `wrapper_names`.
fn ty_inner<'a>(wrapper_names: &[&str], ty: &'a syn::Type) -> Option<&'a syn::Type> {
    if let syn::Type::Path(path) = ty {
        if path.qself.is_some() { return None }
        // Since we only check the last path segment, it isn't necessarily the case that
        // we're referring to `std::vec::Vec` or `std::option::Option`, but there isn't
        // a fool proof way to check these since name resolution happens after macro expansion,
        // so this is likely "good enough" (so long as people don't have their own types called
        // `Option` or `Vec` that take one generic parameter they're looking to parse).
        let last_segment = path.path.segments.last()?;
        let last_segment = last_segment.value();
        if !wrapper_names.iter().any(|name| last_segment.ident == *name) { return None }
        if let syn::PathArguments::AngleBracketed(gen_args) = &last_segment.arguments {
            let generic_arg = gen_args.args.first()?;
            let generic_arg = generic_arg.value();
            if let syn::GenericArgument::Type(ty) = generic_arg {
                return Some(ty)
            }
        }
    }
    None
}

/// Implements `FromArgs` and `SubCommands` for a `#![derive(FromArgs)]` enum.
fn impl_from_args_enum(
    errors: &Errors,
    name: &syn::Ident,
    type_attrs: &TypeAttrs,
    de: &syn::DataEnum,
) -> TokenStream {
    parse_attrs::check_enum_type_attrs(errors, type_attrs, &de.enum_token.span);

    // An enum variant like `<name>(<ty>)`
    struct SubCommandVariant<'a> {
        name: &'a syn::Ident,
        ty: &'a syn::Type,
    }

    let variants: Vec<SubCommandVariant<'_>> = de.variants.iter().filter_map(|variant| {
        parse_attrs::check_enum_variant_attrs(errors, variant);
        let name = &variant.ident;
        let ty = enum_only_single_field_unnamed_variants(errors, &variant.fields)?;
        Some(SubCommandVariant { name, ty })
    }).collect();

    let name_repeating = std::iter::repeat(name.clone());
    let variant_ty_1 = variants.iter().map(|x| x.ty);
    let variant_ty_2 = variant_ty_1.clone();
    let variant_ty_3 = variant_ty_1.clone();
    let variant_names = variants.iter().map(|x| x.name);

    quote! {
        impl argh::FromArgs for #name {
            fn from_args(command_name: &[&str], args: &[&str])
                -> std::result::Result<Self, argh::EarlyExit>
            {
                let subcommand_name = *command_name.last().expect("no subcommand name");
                #(
                    if subcommand_name == <#variant_ty_1 as argh::SubCommand>::COMMAND.name {
                        return Ok(#name_repeating::#variant_names(
                            <#variant_ty_2 as argh::FromArgs>::from_args(command_name, args)?
                        ));
                    }
                )*
                unreachable!("no subcommand matched")
            }
        }

        impl argh::SubCommands for #name {
            const COMMANDS: &'static [&'static argh::CommandInfo] = &[#(
                <#variant_ty_3 as argh::SubCommand>::COMMAND,
            )*];
        }
    }
}

/// Returns `Some(Bar)` if the field is a single-field unnamed variant like `Foo(Bar)`.
/// Otherwise, generates an error.
fn enum_only_single_field_unnamed_variants<'a>(
    errors: &Errors,
    variant_fields: &'a syn::Fields,
) -> Option<&'a syn::Type> {
    macro_rules! with_enum_suggestion {
        ($help_text:literal) => {
            concat!(
                $help_text,
                "\nInstead, use a variant with a single unnamed field for each subcommand:\n",
                "    enum MyCommandEnum {\n",
                "        SubCommandOne(SubCommandOne),\n",
                "        SubCommandTwo(SubCommandTwo),\n",
                "    }",
            )
        }
    }

    match variant_fields {
        syn::Fields::Named(fields) => {
            errors.err(
                fields,
                with_enum_suggestion!(
                    "`#![derive(FromArgs)]` `enum`s do not support variants with named fields."
                ),
            );
            None
        }
        syn::Fields::Unit => {
            errors.err(
                variant_fields,
                with_enum_suggestion!(
                    "`#![derive(FromArgs)]` does not support `enum`s with no variants."
                ),
            );
            None
        }
        syn::Fields::Unnamed(fields) => {
            if fields.unnamed.len() != 1 {
                errors.err(
                    fields,
                    with_enum_suggestion!(
                        "`#![derive(FromArgs)]` `enum` variants must only contain one field."
                    ),
                );
                None
            } else {
                // `unwrap` is okay because of the length check above.
                let first_field = fields.unnamed.first().unwrap();
                let first_field = first_field.value();
                Some(&first_field.ty)
            }
        }
    }
}
