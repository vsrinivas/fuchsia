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
* Data Preconditions or Schema
* Display and Composition

What's missing:

* Dependencies
* Display and Composition

## Contract

**contract** : string

For now, this is akin to a method name. This may change in the future. In links,
module initial data are provided as a value keyed by the contract name (see
below for examples).

A description of known contracts is available [here](known_contracts.md).

## Data Preconditions / Schema

Modules get their input in the form of a JSON document. Either data
preconditions or a schema may be specified to govern how modules are resolved
based on input data.

**data_preconditions** : &lt;any&gt;

Data preconditions are tests evaluated against the JSON data mapped under the
contract name in a link document.

If the value in the precondition dictionary is an object, then the member being
tested must be an object. For any key-value pair defined in the spec, a test is
run against the corresponding member in the input object. All specified tests
must pass.

*Example:*

    "data_preconditions": {
      "recipient": {
        "network": "your social network",
        "group": "close friends"
      }
    }

matches link data

    {
      "share": {
        "content": { ... }
        "recipient": {
          "name": "Aparna Nielsen",
          "network": "your social network",
          "group": "close friends"
        }
      }
    }

If the value is an array, the match succeeds if any member or test in the array
matches the member being tested.

*Example:*

    "data_preconditions": {
      "scheme": [ "http", "https" ],
      "referrer": [
        "https://www.google.com",
        {
          "host": "www.google.com"
        }
      ]
    }

matches link data

    {
      "view": {
        "uri": "https://en.wikipedia.com",
        "scheme": "https",
        "host": "en.wikipedia.com",
        "referrer": {
          "uri": "https://www.google.com",
          "scheme": "https",
          "host": "www.google.com"
        }
      }
    }

If the value is anything else, the match succeeds if the member is exactly equal
(in type and value) to the member being tested.

**data_schema**: &lt;JSON schema&gt;

JSON schemas are described at [json-schema.org](http://json-schema.org). An
interactive scratchpad to experiment with schemas is available at
[jsonschemavalidator.net](http://www.jsonschemavalidator.net/).
