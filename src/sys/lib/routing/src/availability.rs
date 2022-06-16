// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::AvailabilityRoutingError,
    cm_rust::{
        Availability, DirectoryDecl, EventStreamDecl, ExposeDirectoryDecl, ExposeEventStreamDecl,
        ExposeProtocolDecl, ExposeServiceDecl, OfferDeclCommon, OfferDirectoryDecl,
        OfferEventStreamDecl, OfferProtocolDecl, OfferServiceDecl, OfferSource, OfferStorageDecl,
        ProtocolDecl, ServiceDecl, StorageDecl, UseDeclCommon,
    },
    std::convert::From,
};

/// Opaque availability type to define new traits like PartialOrd on.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct AvailabilityState(pub Availability);

/// Allows creating the availability walker from Availability
impl From<Availability> for AvailabilityState {
    fn from(availability: Availability) -> Self {
        // The only entry point should be to this from a use declaration, in which validation will
        // prevent any SameAsTarget values.
        assert_ne!(availability, Availability::SameAsTarget, "availability must be known");
        AvailabilityState(availability)
    }
}

impl AvailabilityState {
    pub fn advance_with_offer<O>(&mut self, offer: &O) -> Result<(), AvailabilityRoutingError>
    where
        O: OfferDeclCommon,
    {
        let next_availability = offer
            .availability()
            .expect("tried to check availability on an offer that doesn't have that field");
        if offer.source() == &OfferSource::Void {
            match self.advance(next_availability) {
                Ok(()) => Err(AvailabilityRoutingError::OfferFromVoidToOptionalTarget),
                Err(AvailabilityRoutingError::OptionalOfferToRequiredTarget) => {
                    Err(AvailabilityRoutingError::OfferFromVoidToRequiredTarget)
                }
                Err(e) => Err(e),
            }
        } else {
            self.advance(next_availability)
        }
    }

    pub fn advance(
        &mut self,
        next_availability: &Availability,
    ) -> Result<(), AvailabilityRoutingError> {
        match (&self.0, &next_availability) {
            (Availability::SameAsTarget, _) =>
                panic!("we should never have an unknown availability"),
            // If our availability doesn't change, there's nothing to do.
            (Availability::Required, Availability::Required)
            | (Availability::Optional, Availability::Optional)
            // If the next availability is explicitly a pass-through, there's nothing to do.
            | (Availability::Required, Availability::SameAsTarget)
            | (Availability::Optional, Availability::SameAsTarget) => (),
            // If we are optional and our parent gives us a required offer, that's fine. We're
            // required now.
            (Availability::Optional, Availability::Required) =>
                self.0 = next_availability.clone(),
            // If we are required and our parent gives us an optional offer, that's a problem
            // because our parent cannot promise us that the capability we need will always be
            // present.
            (Availability::Required, Availability::Optional) =>
                return Err(AvailabilityRoutingError::OptionalOfferToRequiredTarget),
        }
        Ok(())
    }
}

macro_rules! make_availability_visitor {
    ($name:ident, {
        $(OfferDecl => $offer_decl:ty,)*
        $(ExposeDecl => $expose_decl:ty,)*
        $(CapabilityDecl => $cap_decl:ty,)*
    }) => {
        #[derive(Debug, PartialEq, Eq, Clone)]
        pub struct $name(pub AvailabilityState);

        impl $name {
            pub fn new<U>(use_decl: &U) -> Self where U: UseDeclCommon {
                Self(use_decl.availability().clone().into())
            }

            pub fn required() -> Self {
                Self(Availability::Required.into())
            }
        }

        $(
            impl $crate::router::OfferVisitor for $name {
                type OfferDecl = $offer_decl;

                fn visit(&mut self, offer: &Self::OfferDecl) -> Result<(), $crate::error::RoutingError> {
                    self.0.advance_with_offer(offer).map_err(Into::into)
                }
            }
        )*

        $(
            impl $crate::router::ExposeVisitor for $name {
                type ExposeDecl = $expose_decl;

                fn visit(&mut self, _decl: &Self::ExposeDecl) -> Result<(), $crate::error::RoutingError> {
                    Ok(())
                }
            }
        )*

        $(
            impl $crate::router::CapabilityVisitor for $name {
                type CapabilityDecl = $cap_decl;

                fn visit(
                    &mut self,
                    _decl: &Self::CapabilityDecl
                ) -> Result<(), $crate::error::RoutingError> {
                    Ok(())
                }
            }
        )*
    };
}

make_availability_visitor!(AvailabilityServiceVisitor, {
    OfferDecl => OfferServiceDecl,
    ExposeDecl => ExposeServiceDecl,
    CapabilityDecl => ServiceDecl,
});

make_availability_visitor!(AvailabilityProtocolVisitor, {
    OfferDecl => OfferProtocolDecl,
    ExposeDecl => ExposeProtocolDecl,
    CapabilityDecl => ProtocolDecl,
});

make_availability_visitor!(AvailabilityDirectoryVisitor, {
    OfferDecl => OfferDirectoryDecl,
    ExposeDecl => ExposeDirectoryDecl,
    CapabilityDecl => DirectoryDecl,
});

make_availability_visitor!(AvailabilityStorageVisitor, {
    OfferDecl => OfferStorageDecl,
    CapabilityDecl => StorageDecl,
});

make_availability_visitor!(AvailabilityEventStreamVisitor, {
    OfferDecl => OfferEventStreamDecl,
    ExposeDecl => ExposeEventStreamDecl,
    CapabilityDecl => EventStreamDecl,
});

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        cm_rust::{DependencyType, OfferDecl, OfferTarget},
        std::convert::TryInto,
    };

    fn new_offer(availability: Availability) -> OfferDecl {
        OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: "fuchsia.examples.Echo".try_into().unwrap(),
            target: OfferTarget::static_child("echo".to_string()),
            target_name: "fuchsia.examples.Echo".try_into().unwrap(),
            dependency_type: DependencyType::WeakForMigration,
            availability,
        })
    }

    fn new_void_offer() -> OfferDecl {
        OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Void,
            source_name: "fuchsia.examples.Echo".try_into().unwrap(),
            target: OfferTarget::static_child("echo".to_string()),
            target_name: "fuchsia.examples.Echo".try_into().unwrap(),
            dependency_type: DependencyType::WeakForMigration,
            availability: Availability::Optional,
        })
    }

    #[test]
    fn optional_offer_to_optional() {
        let mut optional_state: AvailabilityState = Availability::Optional.into();
        let res = optional_state.advance_with_offer(&new_offer(Availability::Optional));
        assert_matches!(res, Ok(()));
    }

    #[test]
    fn required_offer_to_required() {
        let mut optional_state: AvailabilityState = Availability::Required.into();
        let res = optional_state.advance_with_offer(&new_offer(Availability::Required));
        assert_matches!(res, Ok(()));
    }

    #[test]
    fn required_offer_to_optional() {
        let mut optional_state: AvailabilityState = Availability::Optional.into();
        let res = optional_state.advance_with_offer(&new_offer(Availability::Required));
        assert_matches!(res, Ok(()));
    }

    #[test]
    fn same_as_target_offer_to_optional() {
        let mut optional_state: AvailabilityState = Availability::Optional.into();
        let res = optional_state.advance_with_offer(&new_offer(Availability::SameAsTarget));
        assert_matches!(res, Ok(()));
    }

    #[test]
    fn same_as_target_offer_to_required() {
        let mut optional_state: AvailabilityState = Availability::Required.into();
        let res = optional_state.advance_with_offer(&new_offer(Availability::SameAsTarget));
        assert_matches!(res, Ok(()));
    }

    #[test]
    fn optional_offer_to_required() {
        let mut optional_state: AvailabilityState = Availability::Required.into();
        let res = optional_state.advance_with_offer(&new_offer(Availability::Optional));
        assert_matches!(res, Err(AvailabilityRoutingError::OptionalOfferToRequiredTarget));
    }

    #[test]
    fn void_offer_to_required() {
        let mut optional_state: AvailabilityState = Availability::Required.into();
        let res = optional_state.advance_with_offer(&new_void_offer());
        assert_matches!(res, Err(AvailabilityRoutingError::OfferFromVoidToRequiredTarget));
    }

    #[test]
    fn void_offer_to_optional() {
        let mut optional_state: AvailabilityState = Availability::Optional.into();
        let res = optional_state.advance_with_offer(&new_void_offer());
        assert_matches!(res, Err(AvailabilityRoutingError::OfferFromVoidToOptionalTarget));
    }
}
