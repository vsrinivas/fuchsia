# Fuchsia CTAP Authenticator

This API is primarily based on the API defined in the
[CTAP Specifcation](https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-20210615.html#authenticator-api),
*v2.1-ps-20210615 Section 6*, with some reference to the
[WebAuthn Specification](https://www.w3.org/TR/webauthn-2) for determining
methods, and their parameter types and sizes.

The [CtapAuthenticator](./ctap.fidl) protocol acts as a CTAP level authenticator
for applications to communicate with security key devices, ie, USB security
keys.

## Current Capabilities and Features
This API is still in development and is far from complete. 

It may also continue to change as the CTAP Specification evolves.

Currently the following methods from the CTAP Specification are supported:
* [MakeCredential](./make_credential.fidl): based on
[authenticatorMakeCredential](https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-20210615.html#authenticatorMakeCredential)

* [GetAssertion](./get_assertion.fidl): based on
[authenticatorGetAssertion](https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-20210615.html#authenticatorGetAssertion)

## Design Philosophy
This API has been designed to closely follow the same values and types as
defined in the CTAP specification.

Several enumerated values are defined as byte strings rather than integers or
enumerations, for example `PublicKeyCredentialDescriptor.type`. The
rationale for using byte string in the CTAP specification is discussed in the
[WebAuthn API specification](https://w3c.github.io/webauthn/#sct-domstring-backwards-compatibility):
"enumeration types are not referenced by other parts of the Web IDL because that
would preclude other values from being used without updating this specification
and its implementations".
