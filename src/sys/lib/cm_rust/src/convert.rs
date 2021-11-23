// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To seamlessly map between
// `fuchsia.sys2.ChildDecl` -> `fuchsia.component.decl.Child` (and `OfferDecl` types)
// this crate offers aliases that match old name.

#[allow(unused_imports)]
pub use fidl_fuchsia_component_decl::*;

pub type ComponentDecl = fidl_fuchsia_component_decl::Component;

pub type ProgramDecl = fidl_fuchsia_component_decl::Program;

pub type UseDecl = fidl_fuchsia_component_decl::Use;
pub type UseServiceDecl = fidl_fuchsia_component_decl::UseService;
pub type UseProtocolDecl = fidl_fuchsia_component_decl::UseProtocol;
pub type UseDirectoryDecl = fidl_fuchsia_component_decl::UseDirectory;
pub type UseStorageDecl = fidl_fuchsia_component_decl::UseStorage;
pub type UseEventDecl = fidl_fuchsia_component_decl::UseEvent;
pub type UseEventStreamDecl = fidl_fuchsia_component_decl::UseEventStream;

pub type ExposeDecl = fidl_fuchsia_component_decl::Expose;
pub type ExposeServiceDecl = fidl_fuchsia_component_decl::ExposeService;
pub type ExposeProtocolDecl = fidl_fuchsia_component_decl::ExposeProtocol;
pub type ExposeDirectoryDecl = fidl_fuchsia_component_decl::ExposeDirectory;
pub type ExposeRunnerDecl = fidl_fuchsia_component_decl::ExposeRunner;
pub type ExposeResolverDecl = fidl_fuchsia_component_decl::ExposeResolver;

pub type OfferDecl = fidl_fuchsia_component_decl::Offer;
pub type OfferStorageDecl = fidl_fuchsia_component_decl::OfferStorage;
pub type OfferResolverDecl = fidl_fuchsia_component_decl::OfferResolver;
pub type OfferRunnerDecl = fidl_fuchsia_component_decl::OfferRunner;
pub type OfferServiceDecl = fidl_fuchsia_component_decl::OfferService;
pub type OfferProtocolDecl = fidl_fuchsia_component_decl::OfferProtocol;
pub type OfferDirectoryDecl = fidl_fuchsia_component_decl::OfferDirectory;
pub type OfferEventDecl = fidl_fuchsia_component_decl::OfferEvent;

pub type CapabilityDecl = fidl_fuchsia_component_decl::Capability;
pub type ServiceDecl = fidl_fuchsia_component_decl::Service;
pub type ProtocolDecl = fidl_fuchsia_component_decl::Protocol;
pub type DirectoryDecl = fidl_fuchsia_component_decl::Directory;
pub type StorageDecl = fidl_fuchsia_component_decl::Storage;
pub type RunnerDecl = fidl_fuchsia_component_decl::Runner;
pub type ResolverDecl = fidl_fuchsia_component_decl::Resolver;
pub type EventDecl = fidl_fuchsia_component_decl::Event;

pub type ChildDecl = fidl_fuchsia_component_decl::Child;
pub type CollectionDecl = fidl_fuchsia_component_decl::Collection;
pub type EnvironmentDecl = fidl_fuchsia_component_decl::Environment;

pub type ConfigDecl = fidl_fuchsia_component_decl::Config;
pub type ConfigValueType = fidl_fuchsia_component_decl::ConfigValueType;
pub type ConfigVectorElementType = fidl_fuchsia_component_decl::ConfigVectorElementType;
pub type ConfigBooleanType = fidl_fuchsia_component_decl::ConfigBooleanType;
pub type ConfigUnsigned8Type = fidl_fuchsia_component_decl::ConfigUnsigned8Type;
pub type ConfigUnsigned16Type = fidl_fuchsia_component_decl::ConfigUnsigned16Type;
pub type ConfigUnsigned32Type = fidl_fuchsia_component_decl::ConfigUnsigned32Type;
pub type ConfigUnsigned64Type = fidl_fuchsia_component_decl::ConfigUnsigned64Type;
pub type ConfigSigned8Type = fidl_fuchsia_component_decl::ConfigSigned8Type;
pub type ConfigSigned16Type = fidl_fuchsia_component_decl::ConfigSigned16Type;
pub type ConfigSigned32Type = fidl_fuchsia_component_decl::ConfigSigned32Type;
pub type ConfigSigned64Type = fidl_fuchsia_component_decl::ConfigSigned64Type;
pub type ConfigStringType = fidl_fuchsia_component_decl::ConfigStringType;
pub type ConfigVectorType = fidl_fuchsia_component_decl::ConfigVectorType;

pub type Ref = fidl_fuchsia_component_decl::Ref;
