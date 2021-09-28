// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Functions used to convert types between `fuchsia.component` and `fuchsia.sys2`
/// namespace.
use {
    anyhow::{format_err, Error},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
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
enum_to_fsys!(startup_mode, fcomponent::StartupMode, fsys::StartupMode, Lazy, Eager);

// Generates `on_terminate_to_fsys`.
enum_to_fsys!(on_terminate, fcomponent::OnTerminate, fsys::OnTerminate, None, Reboot);

// Generates `dependency_type_to_fsys`
enum_to_fsys!(
    dependency_type,
    fcomponent::DependencyType,
    fsys::DependencyType,
    Strong,
    Weak,
    WeakForMigration
);

// Generates `event_mode_to_fsys`
enum_to_fsys!(event_mode, fcomponent::EventMode, fsys::EventMode, Async, Sync);

pub fn child_decl_to_fsys(input: fcomponent::ChildDecl) -> fsys::ChildDecl {
    fsys::ChildDecl {
        name: input.name,
        url: input.url,
        startup: input.startup.map(startup_mode_to_fsys),
        environment: input.environment,
        on_terminate: input.on_terminate.map(on_terminate_to_fsys),
        ..fsys::ChildDecl::EMPTY
    }
}

pub fn ref_to_fsys(input: fcomponent::Ref) -> Result<fsys::Ref, Error> {
    Ok(match input {
        fcomponent::Ref::Parent(_) => fsys::Ref::Parent(fsys::ParentRef {}),
        fcomponent::Ref::Self_(_) => fsys::Ref::Self_(fsys::SelfRef {}),
        fcomponent::Ref::Child(child_ref) => fsys::Ref::Child(fsys::ChildRef {
            name: child_ref.name,
            collection: child_ref.collection,
        }),
        fcomponent::Ref::Collection(collection_ref) => {
            fsys::Ref::Collection(fsys::CollectionRef { name: collection_ref.name })
        }
        fcomponent::Ref::Framework(_) => fsys::Ref::Framework(fsys::FrameworkRef {}),
        fcomponent::Ref::Capability(capability_ref) => {
            fsys::Ref::Capability(fsys::CapabilityRef { name: capability_ref.name })
        }
        fcomponent::Ref::Debug(_) => fsys::Ref::Debug(fsys::DebugRef {}),
        _ => {
            return Err(format_err!(
                "Received unknowned fuchsia.component.Ref variant: {:?}",
                input
            ));
        }
    })
}

pub fn offer_decl_to_fsys(input: fcomponent::OfferDecl) -> Result<fsys::OfferDecl, Error> {
    Ok(match input {
        fcomponent::OfferDecl::Service(offer_service_decl) => {
            fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                source: offer_service_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_service_decl.source_name,
                target: offer_service_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_service_decl.target_name,
                ..fsys::OfferServiceDecl::EMPTY
            })
        }
        fcomponent::OfferDecl::Protocol(offer_protocol_decl) => {
            fsys::OfferDecl::Protocol(fsys::OfferProtocolDecl {
                source: offer_protocol_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_protocol_decl.source_name,
                target: offer_protocol_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_protocol_decl.target_name,
                dependency_type: offer_protocol_decl.dependency_type.map(dependency_type_to_fsys),
                ..fsys::OfferProtocolDecl::EMPTY
            })
        }
        fcomponent::OfferDecl::Directory(offer_directory_decl) => {
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
        fcomponent::OfferDecl::Storage(offer_storage_decl) => {
            fsys::OfferDecl::Storage(fsys::OfferStorageDecl {
                source: offer_storage_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_storage_decl.source_name,
                target: offer_storage_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_storage_decl.target_name,
                ..fsys::OfferStorageDecl::EMPTY
            })
        }
        fcomponent::OfferDecl::Runner(offer_runner_decl) => {
            fsys::OfferDecl::Runner(fsys::OfferRunnerDecl {
                source: offer_runner_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_runner_decl.source_name,
                target: offer_runner_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_runner_decl.target_name,
                ..fsys::OfferRunnerDecl::EMPTY
            })
        }
        fcomponent::OfferDecl::Resolver(offer_resolver_decl) => {
            fsys::OfferDecl::Resolver(fsys::OfferResolverDecl {
                source: offer_resolver_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_resolver_decl.source_name,
                target: offer_resolver_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_resolver_decl.target_name,
                ..fsys::OfferResolverDecl::EMPTY
            })
        }
        fcomponent::OfferDecl::Event(offer_event_decl) => {
            fsys::OfferDecl::Event(fsys::OfferEventDecl {
                source: offer_event_decl.source.map(ref_to_fsys).transpose()?,
                source_name: offer_event_decl.source_name,
                target: offer_event_decl.target.map(ref_to_fsys).transpose()?,
                target_name: offer_event_decl.target_name,
                mode: offer_event_decl.mode.map(event_mode_to_fsys),
                filter: offer_event_decl.filter,
                ..fsys::OfferEventDecl::EMPTY
            })
        }
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
    // let dynamic_offers = match input.dynamic_offers {
    //     Some(dynamic_offers) => Some(
    //         dynamic_offers
    //             .into_iter()
    //             .map(offer_decl_to_fsys)
    //             .collect::<Result<Vec<_>, Error>>()?,
    //     ),
    //     None => None,
    // };

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

pub fn collection_ref_to_fsys(input: fcomponent::CollectionRef) -> fsys::CollectionRef {
    fsys::CollectionRef { name: input.name }
}

pub fn child_ref_to_fsys(input: fcomponent::ChildRef) -> fsys::ChildRef {
    fsys::ChildRef { name: input.name, collection: input.collection }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_component as fcomponent, fidl_fuchsia_data as fdata,
        fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys, test_case::test_case,
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
        offer_decl!(fcomponent, Service, {
            source: fcomponent::Ref::Self_(fcomponent::SelfRef {}),
            source_name: "self",
            target: fcomponent::Ref::Parent(fcomponent::ParentRef {}),
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

        offer_decl!(fcomponent, Storage, {
            source: fcomponent::Ref::Self_(fcomponent::SelfRef {}),
            source_name: "self",
            target: fcomponent::Ref::Parent(fcomponent::ParentRef {}),
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

        offer_decl!(fcomponent, Resolver, {
            source: fcomponent::Ref::Self_(fcomponent::SelfRef {}),
            source_name: "self",
            target: fcomponent::Ref::Parent(fcomponent::ParentRef {}),
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
        offer_decl!(fcomponent, Runner, {
            source: fcomponent::Ref::Self_(fcomponent::SelfRef {}),
            source_name: "self",
            target: fcomponent::Ref::Parent(fcomponent::ParentRef {}),
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
        offer_protocol_decl!(fcomponent, {
            source: fcomponent::Ref::Self_(fcomponent::SelfRef {}),
            source_name: "self",
            target: fcomponent::Ref::Parent(fcomponent::ParentRef {}),
            target_name: "parent",
            dependency_type: fcomponent::DependencyType::Weak,
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
        offer_directory_decl!(fcomponent, {
            source: fcomponent::Ref::Self_(fcomponent::SelfRef {}),
            source_name: "self",
            target: fcomponent::Ref::Parent(fcomponent::ParentRef {}),
            target_name: "parent",
            rights: fio2::Operations::Connect | fio2::Operations::ReadBytes,
            subdir: "config",
            dependency_type: fcomponent::DependencyType::Weak,
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
        offer_event_decl!(fcomponent, {
            source: fcomponent::Ref::Self_(fcomponent::SelfRef {}),
            source_name: "self",
            target: fcomponent::Ref::Parent(fcomponent::ParentRef {}),
            target_name: "parent",
            mode: fcomponent::EventMode::Async,
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
    fn test_offer_decl_to_fsys(input: fcomponent::OfferDecl, expected: fsys::OfferDecl) {
        let actual = offer_decl_to_fsys(input);

        assert_eq!(actual.expect("expected Ok Result"), expected);
    }
}
