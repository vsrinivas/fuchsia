# Module Manifests

A Module is a *Fuchsia Component* with a "Module" *Facet*. The purpose of a
Module Facet is to describe the Module's API in a language on which the system
can reflect and compute compatibility with user data and/or other Modules.

Although the Component system includes Facets, which can be used as opaque
interface descriptions and can be queried to discover components that conform to
know APIs, the Module API gives a consistent IDL for components that
specifically integrate into the Modular Runtime and are concerned with
*compatibility with specific instances of user data*.

## Facet Overview

Although a flat dictionary, a Module Facet conceptually contains the following
sections:

* Identity & Module Contract
* Dependencies (other services needed to run)
* Data Preconditions
* Display and Composition

What's missing:

* Dependencies
* Display and Composition

## Contract

**contract** : string

For now, this is akin to a method name. This may change in the future.

## Data Preconditions

Modules get their input in the form of a JSON document.

**data_preconditions** : &lt;dictionary&gt;

For any key-value pair defined in the dictionary, a test is run against the
corresponding member in the input document. All specified tests must pass; there
is not currently a way to specify disjunction at the document level.

If the value in the precondition dictionary is an object, the same property
matching test is performed as was performed for the top-level document.

*Example:*

    "data_preconditions": {
      "recipient": {
        "network": "your social network",
        "group": "close friends"
      }
    }

If the value is an array, the match succeeds if any member or test in the array
matches the member being tested.

*Example:*

    "data_preconditions": {
      "protocol": [ "http", "https" ],
      "referrer": [
        "https://www.google.com",
        {
          "domain": "google.com"
        }
      ]
    }

If the value is anything else, the match succeeds if the member is exactly equal
(in type and value) to the member being tested.
