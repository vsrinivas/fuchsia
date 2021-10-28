## Product Development Kit (PDK) plugin for `ffx`

[Decentralized Product Integration Roadmap](/docs/contribute/roadmap/2021/decentralized_product_integration.md)

[RFC-0124](/docs/contribute/governance/rfcs/0124_decentralized_product_integration_artifact_description_and_propagation.md)


The PDK plugin for `ffx` is an experimental plugin used in the development of
the decentralized product integration roadmap.

The PDK plugin provides a mechanism to make source or prebuilt artifacts from
the Fuchsia source tree or from petal repositories available for the assembly of
a Fuchsia product outside of the Fuchsia source tree.


## Prerequisites

Since the plugin is experimental, it must first be enabled like so:

```
ffx config set experimental_pdk true
```

## Use

The plugin has two commands: update and fetch.


```
ffx pdk update

ffx pdk fetch
```

See `ffx help pdk` for more details.
