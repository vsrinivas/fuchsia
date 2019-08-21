# Factory Store Providers

This directory contains a component that implements the following protocols:

- fuchsia.factory.CastCredentialsFactoryStoreProvider
- fuchsia.factory.MiscFactoryStoreProvider
- fuchsia.factory.PlayReadyFactoryStoreProvider
- fuchsia.factory.WidevineFactoryStoreProvider

Each protocol has an associated configuration file that matches the protocol name that must be
placed in the factory_store_providers package's config data. For example,
fuchsia.factory.CastCredentialsFactoryStoreProvider requires a
fuchsia.factory.CastCredentialsFactoryStoreProvider.config file to exist in the component's
`/config/data` directory. This configuration is used to determine which factory files to expose,
what their names will be once exposed, and how those files are validated before exposure if
necessary.

Each configuration file is a JSON file with the following format:

```json
{
  "files": [
    {
      "path": "<path to file in factory payload>",
      "dest": "<optional path at which to expose file>",
      "validators": [
        {
          "name": "<name of the validator>",
          "args": "<validator arguments, type depends on the validator>",
        }
        ...
      ]
    }
    ...
  ],
}
```

Example Configuration:

```json
{
  "files": [
    {
      "path": "serial.txt",
      "validators": [
        { "name": "text" },
        {
          "name": "size",
          "args": 15
        }
      ]
    },
    {
      "path": "secret.key",
      "dest": "keys/device.key",
      "validators": [
        {
          "name": "size",
          "args": {
              "min": 1000,
              "max": 1024
          }
        }
      ]
    },
    {
      // This file has no validators and will not be processed and exposed.
      "path": "file.unknown"
    }
  ]
}
```

## Validators

The following validators are available for factory files:

### pass

Allows a file to be exposed without validation.

By default, a file must be processed by at least one validator to be exposed. Use this to explicitly
allow a file to be exposed without validation.

### size

Validates a file based on its size in bytes.

The size validator requires "args" to be either

- A number for the exact size of the file or
- A dictionary with "min" and "max" entries to denote the range of sizes the file can be.

### text

Validates that a file is a UTF-8 encoded text file.

See the [validators module](src/validators/mod.rs) for more information on validators.
