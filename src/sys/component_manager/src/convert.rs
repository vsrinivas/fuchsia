// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Functions used to convert types between `fuchsia.component` and `fuchsia.sys2`
/// namespace.
use {
    anyhow::{format_err, Error},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_sys2 as fsys,
};

// Temporary mappings until migration from fuchsia.sys2 -> fuchsia.component is complete.
#[macro_export]
macro_rules! enum_to_fsys {
    ($label:ident, $a:ty , $b:ty, $($id: ident),*) => {
        paste::paste! {
            pub fn [<$label _ to _ fsys>](input: $a) -> $b {
                match input {
                    $( <$a>::$id => <$b>::$id, )*
                }
            }
        }
    };
}

// Generates `startup_mode_to_fsys`.
enum_to_fsys!(startup_mode, fdecl::StartupMode, fsys::StartupMode, Lazy, Eager);

// Generates `on_terminate_to_fsys`.
enum_to_fsys!(on_terminate, fdecl::OnTerminate, fsys::OnTerminate, None, Reboot);

// Generates `dependency_type_to_fsys`
enum_to_fsys!(
    dependency_type,
    fdecl::DependencyType,
    fsys::DependencyType,
    Strong,
    Weak,
    WeakForMigration
);

// Generates `event_mode_to_fsys`
enum_to_fsys!(event_mode, fdecl::EventMode, fsys::EventMode, Async, Sync);

pub fn child_decl_to_fsys(input: fdecl::Child) -> fsys::ChildDecl {
    fsys::ChildDecl {
        name: input.name,
        url: input.url,
        startup: input.startup.map(startup_mode_to_fsys),
        environment: input.environment,
        on_terminate: input.on_terminate.map(on_terminate_to_fsys),
        ..fsys::ChildDecl::EMPTY
    }
}

pub fn ref_to_fsys(input: fdecl::Ref) -> Result<fsys::Ref, Error> {
    Ok(match input {
        fdecl::Ref::Parent(_) => fsys::Ref::Parent(fsys::ParentRef {}),
        fdecl::Ref::Self_(_) => fsys::Ref::Self_(fsys::SelfRef {}),
        fdecl::Ref::Child(child_ref) => fsys::Ref::Child(fsys::ChildRef {
            name: child_ref.name,
            collection: child_ref.collection,
        }),
        fdecl::Ref::Collection(collection_ref) => {
            fsys::Ref::Collection(fsys::CollectionRef { name: collection_ref.name })
        }
        fdecl::Ref::Framework(_) => fsys::Ref::Framework(fsys::FrameworkRef {}),
        fdecl::Ref::Capability(capability_ref) => {
            fsys::Ref::Capability(fsys::CapabilityRef { name: capability_ref.name })
        }
        fdecl::Ref::Debug(_) => fsys::Ref::Debug(fsys::DebugRef {}),
        _ => {
            return Err(format_err!(
                "Received unknowned fuchsia.component.Ref variant: {:?}",
                input
            ));
        }
    })
}

pub fn offer_decl_to_fsys(input: fdecl::Offer) -> Result<fsys::OfferDecl, Error> {
    Ok(match input {
        fdecl::Offer::Service(offer_service_decl) => {
            fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                source: offer_service_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_service_decl.source_name,
                target: offer_service_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_service_decl.target_name,
                ..fsys::OfferServiceDecl::EMPTY
            })
        }
        fdecl::Offer::Protocol(offer_protocol_decl) => {
            fsys::OfferDecl::Protocol(fsys::OfferProtocolDecl {
                source: offer_protocol_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_protocol_decl.source_name,
                target: offer_protocol_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_protocol_decl.target_name,
                dependency_type: offer_protocol_decl.dependency_type.map(dependency_type_to_fsys),
                ..fsys::OfferProtocolDecl::EMPTY
            })
        }
        fdecl::Offer::Directory(offer_directory_decl) => {
            fsys::OfferDecl::Directory(fsys::OfferDirectoryDecl {
                source: offer_directory_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_directory_decl.source_name,
                target: offer_directory_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_directory_decl.target_name,
                rights: offer_directory_decl.rights,
                subdir: offer_directory_decl.subdir,
                dependency_type: offer_directory_decl.dependency_type.map(dependency_type_to_fsys),
                ..fsys::OfferDirectoryDecl::EMPTY
            })
        }
        fdecl::Offer::Storage(offer_storage_decl) => {
            fsys::OfferDecl::Storage(fsys::OfferStorageDecl {
                source: offer_storage_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_storage_decl.source_name,
                target: offer_storage_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_storage_decl.target_name,
                ..fsys::OfferStorageDecl::EMPTY
            })
        }
        fdecl::Offer::Runner(offer_runner_decl) => fsys::OfferDecl::Runner(fsys::OfferRunnerDecl {
            source: offer_runner_decl.source.map(ref_to_fsys).transpose()?,
            source_name: offer_runner_decl.source_name,
            target: offer_runner_decl.target.map(ref_to_fsys).transpose()?,
            target_name: offer_runner_decl.target_name,
            ..fsys::OfferRunnerDecl::EMPTY
        }),
        fdecl::Offer::Resolver(offer_resolver_decl) => {
            fsys::OfferDecl::Resolver(fsys::OfferResolverDecl {
                source: offer_resolver_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_resolver_decl.source_name,
                target: offer_resolver_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_resolver_decl.target_name,
                ..fsys::OfferResolverDecl::EMPTY
            })
        }
        fdecl::Offer::Event(offer_event_decl) => fsys::OfferDecl::Event(fsys::OfferEventDecl {
            source: offer_event_decl.source.map(ref_to_fsys).transpose()?,
            source_name: offer_event_decl.source_name,
            target: offer_event_decl.target.map(ref_to_fsys).transpose()?,
            target_name: offer_event_decl.target_name,
            mode: offer_event_decl.mode.map(event_mode_to_fsys),
            filter: offer_event_decl.filter,
            ..fsys::OfferEventDecl::EMPTY
        }),
        _ => {
            return Err(format_err!(
                "Received unknowned fuchsia.component.OfferDecl variant: {:?}",
                input
            ));
        }
    })
}

pub fn child_args_to_fsys(
    input: fcomponent::CreateChildArgs,
) -> Result<fsys::CreateChildArgs, Error> {
    Ok(fsys::CreateChildArgs {
        numbered_handles: input.numbered_handles,
        dynamic_offers: input
            .dynamic_offers
            .map(|offer_decls| {
                offer_decls.into_iter().map(offer_decl_to_fsys).collect::<Result<Vec<_>, Error>>()
            })
            .transpose()?,
        ..fsys::CreateChildArgs::EMPTY
    })
}

pub fn collection_ref_to_fsys(input: fdecl::CollectionRef) -> fsys::CollectionRef {
    fsys::CollectionRef { name: input.name }
}

pub fn child_ref_to_fsys(input: fdecl::ChildRef) -> fsys::ChildRef {
    fsys::ChildRef { name: input.name, collection: input.collection }
}

// To seamlessly map between
// `fuchsia.sys2.ChildDecl` -> `fuchsia.component.decl.Child` (and `OfferDecl` types)
// this crate offers aliases that match old name.
#[allow(dead_code)] // The 'Offer*Decl' types are only used for tests below.
pub(crate) mod decl {
    #[allow(unused_imports)]
    pub(crate) use fidl_fuchsia_component_decl::*;

    pub(crate) type ChildDecl = fidl_fuchsia_component_decl::Child;
    pub(crate) type OfferDecl = fidl_fuchsia_component_decl::Offer;
    pub(crate) type OfferStorageDecl = fidl_fuchsia_component_decl::OfferStorage;
    pub(crate) type OfferResolverDecl = fidl_fuchsia_component_decl::OfferResolver;
    pub(crate) type OfferRunnerDecl = fidl_fuchsia_component_decl::OfferRunner;
    pub(crate) type OfferServiceDecl = fidl_fuchsia_component_decl::OfferService;
    pub(crate) type OfferProtocolDecl = fidl_fuchsia_component_decl::OfferProtocol;
    pub(crate) type OfferDirectoryDecl = fidl_fuchsia_component_decl::OfferDirectory;
    pub(crate) type OfferEventDecl = fidl_fuchsia_component_decl::OfferEvent;
}

#[cfg(test)]
mod tests {
    use {
        super::decl as fdecl, super::*, fidl_fuchsia_data as fdata, fidl_fuchsia_io2 as fio2,
        fidl_fuchsia_sys2 as fsys, test_case::test_case,
    };

    // Generates OfferDecl variant for Service, Storage, Runner, and Resolver.
    macro_rules! offer_decl {
        ($namespace:ident, $type:ident, {
            source: $source:expr,
            source_name: $source_name:expr,
            target: $target:expr,
            target_name: $target_name:expr,
        }) => {
            paste::paste! {
                $namespace::OfferDecl::$type(
                    $namespace::[<Offer $type Decl>]{
                        source: Some($source),
                        source_name: Some($source_name.to_owned()),
                        target: Some($target),
                        target_name: Some($target_name.to_owned()),
                        ..$namespace::[<Offer $type Decl>]::EMPTY
                    }
                )
            }
        };
    }

    macro_rules! offer_protocol_decl {
        ($namespace:ident, {
            source: $source:expr,
            source_name: $source_name:expr,
            target: $target:expr,
            target_name: $target_name:expr,
            dependency_type: $dependency_type:expr,
        }) => {
            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                source: Some($source),
                source_name: Some($source_name.to_owned()),
                target: Some($target),
                target_name: Some($target_name.to_owned()),
                dependency_type: Some($dependency_type),
                ..$namespace::OfferProtocolDecl::EMPTY
            })
        };
    }

    macro_rules! offer_directory_decl {
        ($namespace:ident, {
            source: $source:expr,
            source_name: $source_name:expr,
            target: $target:expr,
            target_name: $target_name:expr,
            rights: $rights:expr,
            subdir: $subdir:expr,
            dependency_type: $dependency_type:expr,
        }) => {
            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                source: Some($source),
                source_name: Some($source_name.to_owned()),
                target: Some($target),
                target_name: Some($target_name.to_owned()),
                rights: Some($rights),
                subdir: Some($subdir.to_owned()),
                dependency_type: Some($dependency_type),
                ..$namespace::OfferDirectoryDecl::EMPTY
            })
        };
    }

    macro_rules! offer_event_decl {
        ($namespace:ident, {
            source: $source:expr,
            source_name: $source_name:expr,
            target: $target:expr,
            target_name: $target_name:expr,
            mode: $mode:expr,
            filter: $filter:expr,
        }) => {
            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                source: Some($source),
                source_name: Some($source_name.to_owned()),
                target: Some($target),
                target_name: Some($target_name.to_owned()),
                mode: Some($mode),
                filter: Some($filter),
                ..$namespace::OfferEventDecl::EMPTY
            })
        };
    }

    #[test_case(
        offer_decl!(fdecl, Service, {
            source: fdecl::Ref::Self_(fdecl::SelfRef {}),
            source_name: "self",
            target: fdecl::Ref::Parent(fdecl::ParentRef {}),
            target_name: "parent",
        }),
        offer_decl!(fsys, Service, {
            source: fsys::Ref::Self_(fsys::SelfRef {}),
            source_name: "self",
            target: fsys::Ref::Parent(fsys::ParentRef {}),
            target_name: "parent",
        })
        ; "when decl is service")]
    #[test_case(

        offer_decl!(fdecl, Storage, {
            source: fdecl::Ref::Self_(fdecl::SelfRef {}),
            source_name: "self",
            target: fdecl::Ref::Parent(fdecl::ParentRef {}),
            target_name: "parent",
        }),
        offer_decl!(fsys, Storage, {
            source: fsys::Ref::Self_(fsys::SelfRef {}),
            source_name: "self",
            target: fsys::Ref::Parent(fsys::ParentRef {}),
            target_name: "parent",
        })
        ; "when decl is storage"
    )]
    #[test_case(

        offer_decl!(fdecl, Resolver, {
            source: fdecl::Ref::Self_(fdecl::SelfRef {}),
            source_name: "self",
            target: fdecl::Ref::Parent(fdecl::ParentRef {}),
            target_name: "parent",
        }),
        offer_decl!(fsys, Resolver, {
            source: fsys::Ref::Self_(fsys::SelfRef {}),
            source_name: "self",
            target: fsys::Ref::Parent(fsys::ParentRef {}),
            target_name: "parent",
        })
        ; "when decl is resolver"
    )]
    #[test_case(
        offer_decl!(fdecl, Runner, {
            source: fdecl::Ref::Self_(fdecl::SelfRef {}),
            source_name: "self",
            target: fdecl::Ref::Parent(fdecl::ParentRef {}),
            target_name: "parent",
        }),
        offer_decl!(fsys, Runner, {
            source: fsys::Ref::Self_(fsys::SelfRef {}),
            source_name: "self",
            target: fsys::Ref::Parent(fsys::ParentRef {}),
            target_name: "parent",
        })
        ; "when decl is runner"
    )]
    #[test_case(
        offer_protocol_decl!(fdecl, {
            source: fdecl::Ref::Self_(fdecl::SelfRef {}),
            source_name: "self",
            target: fdecl::Ref::Parent(fdecl::ParentRef {}),
            target_name: "parent",
            dependency_type: fdecl::DependencyType::Weak,
        }),
        offer_protocol_decl!(fsys, {
            source: fsys::Ref::Self_(fsys::SelfRef {}),
            source_name: "self",
            target: fsys::Ref::Parent(fsys::ParentRef {}),
            target_name: "parent",
            dependency_type: fsys::DependencyType::Weak,
        })
        ; "when decl is protocol"
    )]
    #[test_case(
        offer_directory_decl!(fdecl, {
            source: fdecl::Ref::Self_(fdecl::SelfRef {}),
            source_name: "self",
            target: fdecl::Ref::Parent(fdecl::ParentRef {}),
            target_name: "parent",
            rights: fio2::Operations::Connect | fio2::Operations::ReadBytes,
            subdir: "config",
            dependency_type: fdecl::DependencyType::Weak,
        }),
        offer_directory_decl!(fsys, {
            source: fsys::Ref::Self_(fsys::SelfRef {}),
            source_name: "self",
            target: fsys::Ref::Parent(fsys::ParentRef {}),
            target_name: "parent",
            rights: fio2::Operations::Connect | fio2::Operations::ReadBytes,
            subdir: "config",
            dependency_type: fsys::DependencyType::Weak,
        })
        ; "when decl is directory"
    )]
    #[test_case(
        offer_event_decl!(fdecl, {
            source: fdecl::Ref::Self_(fdecl::SelfRef {}),
            source_name: "self",
            target: fdecl::Ref::Parent(fdecl::ParentRef {}),
            target_name: "parent",
            mode: fdecl::EventMode::Async,
            filter: fdata::Dictionary::EMPTY,
        }),
        offer_event_decl!(fsys, {
            source: fsys::Ref::Self_(fsys::SelfRef {}),
            source_name: "self",
            target: fsys::Ref::Parent(fsys::ParentRef {}),
            target_name: "parent",
            mode: fsys::EventMode::Async,
            filter: fdata::Dictionary::EMPTY,
        })
        ; "when decl is event"
    )]
    #[test]
    fn test_offer_decl_to_fsys(input: fdecl::OfferDecl, expected: fsys::OfferDecl) {
        let actual = offer_decl_to_fsys(input);

        assert_eq!(actual.expect("expected Ok Result"), expected);
    }
}
