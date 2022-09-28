//! This crate provides an API for talking to repositories that implement The Update Framework
//! (TUF).
//!
//! If you are unfamiliar with TUF, you should read up on it via the [official
//! website](http://theupdateframework.github.io/). This crate aims to implement the entirety of
//! the specification as defined at the [head of the `develop`
//! branch](https://github.com/theupdateframework/tuf/blob/develop/docs/tuf-spec.txt) in the
//! official TUF git repository.
//!
//! Additionally, the following two papers are valuable supplements in understanding how to
//! actually implement TUF for a community repository.
//!
//! - [The Diplomat paper
//! (2016)](https://www.usenix.org/conference/nsdi16/technical-sessions/presentation/kuppusamy)
//! - [The Mercury paper
//! (2017)](https://www.usenix.org/conference/atc17/technical-sessions/presentation/kuppusamy)
//!
//! Failure to read the spec and the above papers will likely lead to an implementation that does
//! not take advantage of all the security guarantees that TUF offers.
//!
//! # Interoperability
//!
//! It should be noted that historically the TUF spec defined exactly one metadata format and one
//! way of organizing metadata within a repository. Thus, all TUF implementation could perfectly
//! interoperate. The TUF spec has moved to describing *how a framework should behave* leaving many
//! of the detais up to the implementor. Therefore, there are **zero** guarantees that this library
//! will work with any other TUF implementation. Should you want to access a TUF repository that
//! uses `rust-tuf` as its backend from another language, ASN.1 modules and metadata schemas are
//! provided that will allow you to interoperate with this library.
//!
//! # Implementation Considerations
//!
//! ## Key Management
//!
//! Part of TUF is that it acts as its own PKI, and there is no integration that needs to be done
//! for managing keys.
//!
//! Note: No two private keys that are generated should ever exist on the same hardware. When a
//! step says "generate `N` keys," the implication is that these `N` keys are generated on `N`
//! devices.
//!
//! The first set of keys that need to be generated at the root keys that are used to sign the root
//! metadata. The root should be defined with the following properties:
//!
//! - Minimum:
//!   - 3 keys
//!   - threshold of 2
//! - Recommended:
//!   - 5 keys
//!   - threshold of 3
//!
//! If a threshold of root keys are compromised, then the entire system is compromised and TUF
//! clients will need to be manually updated. Similarly, if some `X` keys are lost such that the
//! threshold `N` cannot be reached, then clients will also need to be manually updated. Both of
//! situations are considered critically unsafe. Whatever number of keys are used, it should be
//! assumed that some small number may be lost or compromised.
//!
//! These root keys **MUST** be kept offline on secure media.
//!
//! ## Delegations
//!
//! TUF's most useful feature is the ability to delegate certain roles to sign certain targets.
//! This is discussed in extensive detail in the aforementioned Diplomat paper. There are three
//! problems faced when delegating trust in TUF:
//!
//! 1. What to do for existent accounts that have not yet created and signed TUF metadata
//! 2. What to do when a new account registers
//! 3. What to do when an account uploads a new target and new metadata
//!
//! There are several approaches for dealing with the above scenarios. We are only going to discuss
//! on here as it is the recommended approach. This approach is taken directly from Section 6.1 of
//! the Diplomat paper
//!
//! ### Maximum Security Model
//!
//! The top-level targets role delegates to three other roles and are listed in the following order:
//!
//! 1. `claimed-projects`
//!   - Terminating
//!   - Delegates to project-specific roles that have registered keys with TUF
//! 2. `rarely-updated-projects`
//!   - Terminating
//!   - Signs all packages for all projects that have been "abandoned" or left unupdated for a long
//!   time AND have not yet registered keys with TUF
//! 3. `new-projects`
//!   - Non-terminating
//!   - Signs all packages for all new projects as well as projects that were relegated to
//!   `rarely-updated-projects`
//!
//! The top-level `targets` role as well as `claimed-projects` and `rarely-updated-projects`
//! **MUST** all use offline keys.
//!
//! The critical, manual step is to register new projects with TUF keys and move them into the
//! `claimed-projects` role. Projects that refuse to register keys should have their packages
//! periodically moved into the `rarely-updated-projects` role. Projects in either of these two
//! roles are safe from compromise as their keys are offline. Since the keys used for the above
//! operation are kept offline, this is periodic, manual process.
//!
//! ## Snapshot & Timestamp
//!
//! In a community repository, these two keys need to be kept online and will be used to sign new
//! metadata on every update.

#![deny(missing_docs)]
#![allow(
    clippy::collapsible_if,
    clippy::implicit_hasher,
    clippy::let_unit_value,
    clippy::new_ret_no_self,
    clippy::op_ref,
    clippy::too_many_arguments
)]

pub mod client;
pub mod crypto;
pub mod database;
pub mod error;
pub mod metadata;
pub mod pouf;
pub mod repo_builder;
pub mod repository;
pub mod verify;

mod format_hex;
mod util;

pub use crate::database::*;
pub use crate::error::*;
