// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To seamlessly map between
// `fuchsia.sys2.ChildDecl` -> `fuchsia.component.decl.Child` (and `OfferDecl` types)
// this crate offers aliases that match old name.

#[allow(unused_imports)]
pub(crate) use fidl_fuchsia_component_decl::*;

pub(crate) type ComponentDecl = fidl_fuchsia_component_decl::Component;

pub(crate) type ProgramDecl = fidl_fuchsia_component_decl::Program;

pub(crate) type UseDecl = fidl_fuchsia_component_decl::Use;
pub(crate) type UseServiceDecl = fidl_fuchsia_component_decl::UseService;
pub(crate) type UseProtocolDecl = fidl_fuchsia_component_decl::UseProtocol;
pub(crate) type UseDirectoryDecl = fidl_fuchsia_component_decl::UseDirectory;
pub(crate) type UseStorageDecl = fidl_fuchsia_component_decl::UseStorage;
pub(crate) type UseEventDecl = fidl_fuchsia_component_decl::UseEvent;
pub(crate) type UseEventStreamDecl = fidl_fuchsia_component_decl::UseEventStream;

pub(crate) type ExposeDecl = fidl_fuchsia_component_decl::Expose;
pub(crate) type ExposeServiceDecl = fidl_fuchsia_component_decl::ExposeService;
pub(crate) type ExposeProtocolDecl = fidl_fuchsia_component_decl::ExposeProtocol;
pub(crate) type ExposeDirectoryDecl = fidl_fuchsia_component_decl::ExposeDirectory;
pub(crate) type ExposeRunnerDecl = fidl_fuchsia_component_decl::ExposeRunner;
pub(crate) type ExposeResolverDecl = fidl_fuchsia_component_decl::ExposeResolver;

pub(crate) type OfferDecl = fidl_fuchsia_component_decl::Offer;
pub(crate) type OfferStorageDecl = fidl_fuchsia_component_decl::OfferStorage;
pub(crate) type OfferResolverDecl = fidl_fuchsia_component_decl::OfferResolver;
pub(crate) type OfferRunnerDecl = fidl_fuchsia_component_decl::OfferRunner;
pub(crate) type OfferServiceDecl = fidl_fuchsia_component_decl::OfferService;
pub(crate) type OfferProtocolDecl = fidl_fuchsia_component_decl::OfferProtocol;
pub(crate) type OfferDirectoryDecl = fidl_fuchsia_component_decl::OfferDirectory;
pub(crate) type OfferEventDecl = fidl_fuchsia_component_decl::OfferEvent;

pub(crate) type CapabilityDecl = fidl_fuchsia_component_decl::Capability;
pub(crate) type ServiceDecl = fidl_fuchsia_component_decl::Service;
pub(crate) type ProtocolDecl = fidl_fuchsia_component_decl::Protocol;
pub(crate) type DirectoryDecl = fidl_fuchsia_component_decl::Directory;
pub(crate) type StorageDecl = fidl_fuchsia_component_decl::Storage;
pub(crate) type RunnerDecl = fidl_fuchsia_component_decl::Runner;
pub(crate) type ResolverDecl = fidl_fuchsia_component_decl::Resolver;
pub(crate) type EventDecl = fidl_fuchsia_component_decl::Event;

pub(crate) type ChildDecl = fidl_fuchsia_component_decl::Child;
pub(crate) type CollectionDecl = fidl_fuchsia_component_decl::Collection;
pub(crate) type EnvironmentDecl = fidl_fuchsia_component_decl::Environment;

pub(crate) type Ref = fidl_fuchsia_component_decl::Ref;
