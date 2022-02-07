// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! WPA credential data mappings.
//!
//! This modules defines the types of credential data used by the [`Wpa`] type constructor. [`Wpa`]
//! accepts a single type parameter that must implement the [`CredentialData`] trait. This trait
//! defines associated types for each WPA suite (i.e., WPA Personal and WPA Enterprise) that in
//! turn define associated types for each version of WPA. These types determine what, if any, data
//! is contained in a [`Wpa`] instance.
//!
//! [`CredentialData`]: crate::security::wpa::data::CredentialData
//! [`Wpa`]: crate::security::wpa::Wpa

use std::fmt::Debug;

use crate::security::wpa::{
    Wpa1PersonalCredentials, Wpa2PersonalCredentials, Wpa3PersonalCredentials,
};

/// Defines the credential data used by [`WpaAuthenticator`].
///
/// This ZST specifies WPA Personal and WPA Enterprise types that contain credential data when used
/// with the [`Wpa`] type constructor and is part of the [`WpaAuthenticator`] type definition. The
/// conjugate type used for [`WpaDescriptor`] is the unit type `()`, which uses the unit type for
/// all credential data (that is, no data at all).
///
/// [`WpaAuthenticator`]: crate::security::wpa::WpaAuthenticator
/// [`WpaDescriptor`]: crate::security::wpa::WpaDescriptor
pub enum AuthenticatorData {}

/// Describes the data contained within a [`Wpa`] instance.
pub trait CredentialData {
    type Personal: PersonalData;
    type Enterprise: EnterpriseData;
}

impl CredentialData for () {
    type Personal = ();
    type Enterprise = ();
}

impl CredentialData for AuthenticatorData {
    type Personal = Self;
    type Enterprise = Self;
}

// The additional type bounds on associated types limits the constraints required for `Wpa`'s
// implementation of the same traits. See the `Copy` implementation for `Wpa` for an example where
// these bounds cannot be applied to these associated types.
pub trait PersonalData {
    type Wpa1: Clone + Debug + Eq;
    type Wpa2: Clone + Debug + Eq;
    type Wpa3: Clone + Debug + Eq;
}

impl PersonalData for () {
    type Wpa1 = ();
    type Wpa2 = ();
    type Wpa3 = ();
}

impl PersonalData for AuthenticatorData {
    type Wpa1 = Wpa1PersonalCredentials;
    type Wpa2 = Wpa2PersonalCredentials;
    type Wpa3 = Wpa3PersonalCredentials;
}

pub trait EnterpriseData {
    type Wpa2: Clone + Debug + Eq;
    type Wpa3: Clone + Debug + Eq;
}

impl EnterpriseData for () {
    type Wpa2 = ();
    type Wpa3 = ();
}

impl EnterpriseData for AuthenticatorData {
    type Wpa2 = ();
    type Wpa3 = ();
}
