// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::frame_writer::BufferWrite,
    quote::quote,
    syn::{
        parse::{Parse, ParseStream},
        Error, Expr, Ident, Result, Token,
    },
};

const IE_SSID: &str = "ssid";
const IE_HT_CAPABILITIES: &str = "ht_cap";
const IE_VHT_CAPABILITIES: &str = "vht_cap";
const IE_SUPPORTED_RATES: &str = "supported_rates";
const IE_SUPPORTED_EXTENDED_RATES: &str = "extended_supported_rates";
const IE_RSNE: &str = "rsne";
const IE_BSS_MAX_IDLE_PERIOD: &str = "bss_max_idle_period";
const IE_DSSS_PARAM_SET: &str = "dsss_param_set";
const IE_WPA1: &str = "wpa1";
const IE_WSC: &str = "wsc";
const IE_TIM: &str = "tim";

/// Field carrying necessary meta information to generate relevant tokens.
#[derive(Hash, Eq, Ord, PartialEq, PartialOrd, Clone, Copy)]
pub enum Ie {
    Ssid,
    HtCaps,
    VhtCaps,
    Rates,
    ExtendedRates {
        // `true` if supported rates should be continued.
        // `false` if the user specified a dedicated extended supported rates set.
        continue_rates: bool,
    },
    Rsne,
    BssMaxIdlePeriod,
    DsssParamSet,
    Wpa1,
    Wsc,
    Tim,
}

pub struct IeDefinition {
    pub name: Ident,
    pub type_: Ie,
    pub emit_offset: Option<Ident>,
    value: Expr,
    optional: bool,
}

/// Declares a new local variable `$name` by evaluating the given expression `$expr`.
/// If-Expressions are treated specially:
/// * if the IE is `$optional` and an else-branch was provided the return type is expected to be
///   `Option<V>` with `V` representing the IE's value type.
/// * if no else-branch was provided the return type must *not* be an `Option<_>`.
///   Instead, if the if-expression's condition evaluates to `true` the then-branch is evaluated
///   and wrapped with `Some(_)`.
///   If the if-expression's condition evaluates to `false` None will be used instead and the
///   then-branch is not executed.
/// * if the IE is *not* `$optional` and an else-branch was specified the expression must yield a
///   type compatible with the IE's expected value type.
macro_rules! declare_var {
    ($name:expr, $optional:expr, $expr:expr) => {
        match $expr {
            Expr::If(if_expr) => {
                if if_expr.else_branch.is_some() {
                    if $optional {
                        quote!(let $name: Option<_> = #if_expr;)
                    } else {
                        quote!(let $name = #if_expr;)
                    }
                } else {
                    let cond = &if_expr.cond;
                    let v = &if_expr.then_branch;
                    quote!(
                        let mut $name: Option<_> = if #cond {
                            Some(#v)
                        } else { None };
                    )
                }
            }
            other => quote!(let $name = #other;),
        }
    };
    ($name:expr, $expr:expr) => { declare_var!($name, false, $expr) };
}

/// Generates tokens to add a length evaluated through the expression `$len` to a previously
/// declared mutable, local variable `frame_len`. If `$optional` evaluates to `true` the value
/// stored in `$name` is assumed to be of type `Option<V>` and its length will be added
/// conditionally based on the value's presence.
macro_rules! frame_len {
    ($name:expr, $optional:expr, $len:expr) => {
        if $optional {
            quote!(frame_len += $name.as_ref().map_or(0, |$name| $len);)
        } else {
            quote!(frame_len += $len;)
        }
    };
    ($name:expr, $len:expr) => {
        frame_len! ($name, false, $len)
    };
}

/// Executes an expression `$func`. If `$optional` evaluates to `true` the value stored in `$name`
/// is assumed to be of type `Option<V>`. In this case, `$func` is only executed if the value is
/// present. The expression can assume access to the Option's unwrapped value.
/// `$expr` represents the IE's value expression.
macro_rules! apply_on {
    ($name:expr, $optional:expr, $expr:expr, $func:expr) => {
        if $optional {
            quote!(if let Some($name) = $name {
                $func;
            })
        } else {
            match $expr {
                Expr::If(if_expr) => {
                    if if_expr.else_branch.is_some() {
                        quote!($func;)
                    } else {
                        quote!(if let Some($name) = $name {
                            $func;
                        })
                    }
                }
                _ => quote!($func;),
            }
        }
    };
    ($name:expr, $expr:expr, $func:expr) => {
        apply_on! ($name, false, $expr, $func)
    };
}

impl BufferWrite for IeDefinition {
    fn gen_frame_len_tokens(&self) -> Result<proc_macro2::TokenStream> {
        Ok(match self.type_ {
            Ie::Ssid => frame_len!(ssid, IE_PREFIX_LEN + ssid.len()),
            Ie::HtCaps => {
                frame_len!(ht_caps, self.optional, IE_PREFIX_LEN + size_of::<ie::HtCapabilities>())
            }
            Ie::VhtCaps => frame_len!(
                vht_caps,
                self.optional,
                IE_PREFIX_LEN + size_of::<ie::VhtCapabilities>()
            ),
            // rsne::Rsne#len() carries the IE header already.
            Ie::Rsne => frame_len!(rsne, self.optional, rsne.len()),
            Ie::Rates => frame_len!(
                rates,
                IE_PREFIX_LEN + std::cmp::min(SUPPORTED_RATES_MAX_LEN, rates.len())
            ),
            Ie::ExtendedRates { continue_rates } => {
                if continue_rates {
                    quote!(if rates.len() > SUPPORTED_RATES_MAX_LEN {
                        frame_len += IE_PREFIX_LEN + rates.len() - SUPPORTED_RATES_MAX_LEN;
                    })
                } else {
                    quote!(frame_len += IE_PREFIX_LEN + extended_supported_rates.len();)
                }
            }
            Ie::BssMaxIdlePeriod => frame_len!(
                bss_max_idle_period,
                self.optional,
                IE_PREFIX_LEN + size_of::<ie::BssMaxIdlePeriod>()
            ),
            Ie::DsssParamSet => {
                frame_len!(bss_max_idle_period, IE_PREFIX_LEN + size_of::<ie::DsssParamSet>())
            }
            Ie::Wpa1 => {
                // IE + Vendor OUI + Vendor specific type + WPA1 body
                frame_len!(wpa1, self.optional, IE_PREFIX_LEN + 4 + wpa1.len())
            }
            Ie::Wsc => {
                // IE + Vendor OUI + Vendor specific type + WSC body
                frame_len!(wsc, self.optional, IE_PREFIX_LEN + 4 + wsc.len())
            }
            Ie::Tim => frame_len!(
                tim,
                IE_PREFIX_LEN + std::mem::size_of::<ie::TimHeader>() + tim.bitmap.len()
            ),
        })
    }

    fn gen_write_to_buf_tokens(&self) -> Result<proc_macro2::TokenStream> {
        let write_to_buf_tokens = match self.type_ {
            Ie::Ssid => apply_on!(ssid, &self.value, ie::write_ssid(&mut w, &ssid)?),
            Ie::Rates => quote!(
                let rates_writer = ie::RatesWriter::try_new(&rates[..])?;
                rates_writer.write_supported_rates(&mut w);
            ),
            Ie::ExtendedRates { continue_rates } => {
                if continue_rates {
                    quote!(rates_writer.write_ext_supported_rates(&mut w);)
                } else {
                    // Extended supported rates should only be written if the maximum supported
                    // rates were specified.
                    quote!(
                        if rates.len() != SUPPORTED_RATES_MAX_LEN {
                            return Err(FrameWriteError::new_invalid_data(format!(
                                "attempt to write extended_supported_rates without specifying the \
                                maximum allowed supported_rates: {}", rates.len()
                            )).into());
                        }
                        ie::write_ext_supported_rates(&mut w, &extended_supported_rates[..])?;
                    )
                }
            }
            Ie::HtCaps => apply_on!(
                ht_caps,
                self.optional,
                &self.value,
                ie::write_ht_capabilities(&mut w, &ht_caps)?
            ),
            Ie::VhtCaps => apply_on!(
                vht_caps,
                self.optional,
                &self.value,
                ie::write_vht_capabilities(&mut w, &vht_caps)?
            ),
            Ie::Rsne => apply_on!(rsne, self.optional, &self.value, ie::write_rsne(&mut w, &rsne)?),
            Ie::BssMaxIdlePeriod => apply_on!(
                bss_max_idle_period,
                self.optional,
                &self.value,
                ie::write_bss_max_idle_period(&mut w, &bss_max_idle_period)?
            ),
            Ie::DsssParamSet => {
                apply_on!(dsss, &self.value, ie::write_dsss_param_set(&mut w, &dsss)?)
            }
            Ie::Wpa1 => {
                apply_on!(wpa1, self.optional, &self.value, ie::write_wpa1_ie(&mut w, &wpa1)?)
            }
            Ie::Wsc => apply_on!(wsc, self.optional, &self.value, ie::write_wsc_ie(&mut w, &wsc)?),
            Ie::Tim => {
                apply_on!(tim, &self.value, ie::write_tim(&mut w, &tim.header, &tim.bitmap)?)
            }
        };

        let emit_offset = match &self.emit_offset {
            None => quote!(),
            Some(ident) => quote!(#ident = w.bytes_written();),
        };
        Ok(quote!(
            #emit_offset
            #write_to_buf_tokens
        ))
    }

    fn gen_var_declaration_tokens(&self) -> Result<proc_macro2::TokenStream> {
        Ok(match self.type_ {
            Ie::Ssid => declare_var!(ssid, self.optional, &self.value),
            Ie::HtCaps => declare_var!(ht_caps, self.optional, &self.value),
            Ie::VhtCaps => declare_var!(vht_caps, self.optional, &self.value),
            Ie::Rates => declare_var!(rates, &self.value),
            Ie::ExtendedRates { .. } => declare_var!(extended_supported_rates, &self.value),
            Ie::Rsne => declare_var!(rsne, self.optional, &self.value),
            Ie::BssMaxIdlePeriod => declare_var!(bss_max_idle_period, self.optional, &self.value),
            Ie::DsssParamSet => declare_var!(dsss, &self.value),
            Ie::Wpa1 => declare_var!(wpa1, self.optional, &self.value),
            Ie::Wsc => declare_var!(wsc, self.optional, &self.value),
            Ie::Tim => declare_var!(tim, &self.value),
        })
    }
}

impl Parse for IeDefinition {
    fn parse(input: ParseStream<'_>) -> Result<Self> {
        let mut emit_offset = None;
        if input.peek2(Token![@]) {
            emit_offset = Some(input.parse::<Ident>()?);
            input.parse::<Token![@]>()?;
        }

        let name = input.parse::<Ident>()?;

        let optional = input.peek(Token![?]);
        if optional {
            input.parse::<Token![?]>()?;
        }
        input.parse::<Token![:]>()?;
        let value = input.parse::<Expr>()?;

        match value {
            Expr::Block(_)
            | Expr::Call(_)
            | Expr::If(_)
            | Expr::Lit(_)
            | Expr::MethodCall(_)
            | Expr::Reference(_)
            | Expr::Repeat(_)
            | Expr::Struct(_)
            | Expr::Tuple(_)
            | Expr::Unary(_)
            | Expr::Index(_)
            | Expr::Match(_)
            | Expr::Path(_) => (),
            other => {
                return Err(Error::new(
                    name.span(),
                    format!("invalid expression for IE value: {:?}", other),
                ))
            }
        }
        let type_ = match name.to_string().as_str() {
            IE_SSID => {
                if optional {
                    return Err(Error::new(name.span(), "`ssid` IE may never be optional"));
                }
                Ie::Ssid
            }
            IE_DSSS_PARAM_SET => {
                if optional {
                    return Err(Error::new(
                        name.span(),
                        "`dsss_param_set` IE may never be optional",
                    ));
                }
                Ie::DsssParamSet
            }
            IE_HT_CAPABILITIES => Ie::HtCaps,
            IE_VHT_CAPABILITIES => Ie::VhtCaps,
            IE_SUPPORTED_RATES | IE_SUPPORTED_EXTENDED_RATES if optional => {
                return Err(Error::new(
                    name.span(),
                    "`supported_rates` and `extended_supported_rates` IE may never be optional",
                ));
            }
            IE_SUPPORTED_RATES => Ie::Rates,
            IE_SUPPORTED_EXTENDED_RATES => {
                let continue_rates = match &value {
                    Expr::Block(block) => block.block.stmts.is_empty(),
                    _ => false,
                };

                Ie::ExtendedRates { continue_rates }
            }
            IE_TIM => {
                if optional {
                    return Err(Error::new(name.span(), "`tim` IE may never be optional"));
                }
                Ie::Tim
            }
            IE_RSNE => Ie::Rsne,
            IE_BSS_MAX_IDLE_PERIOD => Ie::BssMaxIdlePeriod,
            IE_WPA1 => Ie::Wpa1,
            IE_WSC => Ie::Wsc,
            unknown => return Err(Error::new(name.span(), format!("unknown IE: '{}'", unknown))),
        };
        Ok(IeDefinition { name, value, type_, optional, emit_offset })
    }
}
