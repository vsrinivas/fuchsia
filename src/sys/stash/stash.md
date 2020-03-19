# Stash usage

## Should I use Stash?

Stash may be used by system services which require access to mutable storage
before user login, as Stash stores its data on the device's data partition.
Once a user has logged in, such data should go in the user's encrypted
partition.

Components that can use an alternative to Stash, should.

*   Storage in Stash is not attributed to the component that wrote it.
    Clients should use Stash with care, or they risk filling up a mutable
    storage partition without attribution.
*   Reading state from Stash is not restricted to the component that wrote it.
    Clients should use Stash with care, or they risk overwriting another
    client's state, or establishing a side channel over Stash.

If you're unsure if your system service should use Stash or not, reach out to
the security team.

## How do I use Stash?

Stash doesn't have any client libraries to assist in using it, so just use the
[FIDL bindings][fidl] directly. For example usage in Rust, check out the
[`stash_ctl` command][stash_ctl].

## Which interface to Stash should I use?

There are two instances of Stash, each with separate backing files. One is
accessible over `fuchsia.stash.Store` and the other is accessible over
`fuchsia.stash.SecureStore`. Both behave identically, with the exception that
the latter interface doesn't allow storage or retrieval of values holding
arbitrary bytes. This is because the `SecureStore` version is intended for use
by things in the critical path of updates, and we wish to discourage the usage
of complex parsers on unsigned data in this path.

You should use the `Store` version of the interface, unless you've been told
otherwise by the security team.

## Testing with Stash

When the [`test_package` GN template][test_package] is used to set up a hermetic
environment for running tests, a standalone instance of Stash can be set up in
this environment to run tests against.

An example component manifest for setting up this environment is as follows:

```
{
  "program": {
    "binary": "test/my-apps-unittests"
  },
  "sandbox": {
    "services": [
      "fuchsia.stash.Store"
    ]
  },
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        "fuchsia.stash.Store":
          "fuchsia-pkg://fuchsia.com/stash#meta/stash.cmx"
      }
    }
  }
}
```

For a real-world example of accessing stash from tests, take a look at [the
Stash unit tests in `bt-gap`][stash.rs]

## Client identity in Stash

Right now all clients must call `Identify` when they connect to Stash, and
provide a name under which their data is stored. This is currently enforced by
an honor system, and clients should NOT use this to access the stores of other
clients. In the future component monikers (or something else TBD) will be used
to enforce that components cannot access data from other components.

[fidl]: ../../public/fidl/fuchsia.stash/stash.fidl
[stash_ctl]: ../stash_ctl
[dgonyeo]: mailto:dgonyeo@google.com
[joshlf]: mailto:joshlf@google.com
[test_package]: /docs/development/tests/test_component.md
[stash.rs]: ../bluetooth/bt-gap/src/store/stash.rs
