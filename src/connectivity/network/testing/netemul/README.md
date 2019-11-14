# Netemul Runner

Netemul Runner is a set of components that allow creating hermetic test environments for
integration tests.

Besides creating sandboxed and hermetic environments, Netemul provides a set of
[services](#services) that allow for quick and easy network emulation that can be used to manage
virtual networks between sandboxed environments. The hermetic environments with the virtual
networks between them can be used to emulate multiple fuchsia devices talking to each other over
networks and, thus, allows for self-contained integration tests.

## Layout

Two components are provided `netemul_runner` and `netemul_sandbox`:

`netemul_runner` can be used as a runner, as defined by the component framework,
and provides the FIDL service *fuchsia.sys.Runner*. It expects the `program.data` field of your
test component's `cmx` manifest to point to another `cmx` file within the same component package.

`netemul_sandbox` is responsible for creating sandboxed environments that can be configured to
run integration tests. `netemul_sandbox` has two modes of operation: enclosing process or service
provider, and it is selected based on its command-line arguments.

In enclosing process mode, `netemul_sandbox` receives a component as a fuchsia package url as a
command-line argument and proceeds to create a hermetic environment and then launch the provided
component within it. The exit code of the `netemul_sandbox` process will mimic the component's. When
using enclosing process, clients will typically setup a layout for the test using the netemul
[facet](#facet) in the component-under-test's cmx manifest.

In service provider mode, `netemul_sandbox` will expose the
[fuchsia.netemul.sandbox.Sandbox](lib/fidl/sandbox.fidl) protocol that allows
users to create netemul's managed environments or run full sandboxes from components.

In sum, users have two options to use netemul for a given component under test:
* use `netemul_runner`, which will use `netemul_sandbox` in enclosing process mode and spawn the
component under test *within* a netemul environment. See [Runner sample setup](#runner-sample-setup)
for an example of how to run a component like this.
* use `netemul_sandbox` as a service provider injected into the component under test's environment
by using the standard `fuchsia.test` facet. The component under test can then simply use the provided
[fuchsia.netemul.sandbox.Sandbox](lib/fidl/sandbox.fidl) service to spawn and
control hermetic environments and emulated networks.

## Runner sample setup

To have a test run on the sandbox, you'll typically use the following pattern:

* Use the build rule `test_package`
* For every test, you'll typically have at least 2 `.cmx` files:
* `[my_component].cmx`, where *my_component* is the component you want to run inside the hermetic
environment. It should be indistinguishable from a regular component manifest. You can run multiple
different components inside a netemul environment by simply using their package url. For this example,
though, we'll only have the one `[my_component].cmx`.
* `[my_test].cmx`, in turn will contain a `fuchsia.netemul` [facet](#facet) that defines what environments
to create and which tests to run and, also, will tell the component launcher to use `netemul` to
run it:
```json
{
    "program": {
        "binary": "bin/app"
    },
    "runner": "fuchsia-pkg://fuchsia.com/netemul_runner#meta/netemul_runner.cmx",
    "facets" : {
      "fuchsia.netemul" : {
        "environment" : {
          "test" : [{"url" : "fuchsia-pkg://fuchsia.com/my_component#meta/my_component.cmx"}]
        }
      }
    }
}
```

`netemul_sandbox`'s [tests](runner/test)
use the pattern thoroughly and can be a good source of examples.

*Note 1*: the `bin/app` entry is just there to match the manifest schema, it is not used nor points
to anything of importance.

*Note 2*: Make sure to match the build file pattern used [here](runner/test/BUILD.gn) for tests that are
defined solely by a cmx manifest, otherwise you risk your test being enumerated by the integration
test framework.


## Sandbox service sample setup

If your component under test doesn't need to (or shouldn't) be *inside* a netemul environment, you
can use the  [fuchsia.netemul.sandbox.Sandbox](lib/fidl/sandbox.fidl) service
by injecting it into your test component's environment. Your component's cmx manifest will look like:
```json
{
    "facets": {
        "fuchsia.test": {
            "injected-services": {
                "fuchsia.netemul.sandbox.Sandbox": "fuchsia-pkg://fuchsia.com/netemul_sandbox#meta/netemul_sandbox.cmx"
            }
        }
    },
    "program": {
        "binary": "test/my_test"
    },
    "sandbox": {
        "services": [
            "fuchsia.netemul.sandbox.Sandbox"
        ]
    }
}
```

The `fuchsia.test` facet when run by `run_test_component` (the standard way of running tests) will
spawn an instance of the sandbox service for use by the component under test.

A rust example of how to use the sandbox service can be found [here](runner/test/sandbox_service/src/main.rs).


## Services

The sandboxed environments created by `netemul_sandbox` will always contain the following FIDL services
that can be used to manipulate and configure the environment. Just don't forget to add them to your
`.cmx`'s *sandbox* parameters to have access.

### NetworkContext

`netemul_sandbox` creates a **single** *NetworkContext* instance (see
[fuchsia.netemul.network](lib/fidl/network.fidl))
and makes it available to **all** environments that are created under it. That is to say, *NetworkContext*
intentionally breaks hermeticity. This service is the core motivator behind *Netemul Runner*, as it
is able to create virtual networks that can be used to connect different hermetic test environments.

A valid pattern is to have a root environment configure the network setup and then spawn children
to use it or, alternatively, the virtual networks can be setup using the configuration
[facet](#facet).

### Managed Environment

Every environment in the sandbox will be exposed to the service
[fuchsia.netemul.environment.ManagedEnvironment](lib/fidl/environment.fidl).
You can use the provided service to launch other child environments (which may or may not inherit settings).
**Every** managed environment is completely hermetic from a services perspective, apart from
basic configuration that may be inherited. If you wish to have service inheritance in your spawned
children environments, refer to *fuchsia.sys.Environment*. In other words, managed environments **never** inherit
their parent's services in the same sense as *fuchsia.sys.Environment* does, that is, inheriting
the same **instance** of a service. Rather, managed environments may *optionally* inherit their parent's
service *configuration*, that is, the configuration of which services to launch in the environment.

The *ManagedEnvironment* service also provides a special *fuchsia.sys.Launcher* instance that provides
extended functionality, such as forwarding *VirtualDevice* instances in a `/vdev` path and mounting
a memfs under `/vdata` for components that request *dev* and *isolated-persistent-storage*, respectively.

*VirtualDevice* instances are a `vfs` hook for [NetworkContext](#networkcontext)'s *Endpoints*. That
allows for clients to expose specific *Endpoints* on the `vfs` under the created root folder `/vdev`.
This feature is used to test components that perform `vfs` scanning to retrieve devices with minimal
intrusion. This is used to go around the limitation that `/dev` is never hermetic to sandboxed environments.

The ManagedEnvironment also provides hermetic `fuchsia.logger.LogSink` and `fuchsia.logger.Log` services so
that environments can run components that use syslog. Users have the ability to enable and customize log
printing from syslog -- they can also completely disable syslog outputs. See [LoggerOptions](#loggeroptions)
for configuration information.

### SyncManager

Along the same lines as *NetworkContext*, `netemul_sandbox` creates a **single** *Syncmanager* instance
(see [fuchsia.netemul.sync](lib/fidl/sync.fidl))
that is shared between all created environments. Clients can (and are encouraged to) use the *Bus*
service or the other provided primitives to synchronize multiple test agents that are spawned
across different environments.

For example
(see [easy_nestack_cfg test](runner/test/easy_netstack_cfg/src/main.rs)
for working code), a test that creates a TCP *server* on one environment and a *client* on another is encouraged
to use the *Bus* service to have the *server* communicate with the *client* out-of-band first and ensure it's
actually ready to listen for incoming connections.

## Facet

`netemul_sandbox` will look for the facet *fuchsia.netemul* on the `.cmx` file your provide to it.
The facet can be used to build and configure the testing environment before your code gets called,
thus decreasing the amount of boilerplate setup for every test.

Below is the documentation of the objects accepted. The root object is of type [Config](#config)

### Config


| Field       | Type                        | Description                                                                                          |
|-------------|-----------------------------|------------------------------------------------------------------------------------------------------|
| default_url | String                      | Global default URL, will be used by any instance of [LaunchArgs](#launchargs)                        |
| disabled    | Boolean                     | if true, no tests or environments will be created and sandbox will exit cleanly. defaults to *false* |
| timeout     | Number                      | Optional global timeout for all tests to complete, in seconds.                                       |
| capture     | String or Boolean           | Capture network traffic on emulated networks. See below for possible values. Defaults to `"NO"`      |
| environment | [Environment](#environment) | root environment configuration                                                                       |
| networks    | Array of [Network](#network)| collection of networks to setup                                                                      |

`capture` values:
* `"ALWAYS"` will always generate network dumps once sandbox exits.
* `"ON_ERROR"` will only generate dumps if the suite failed for any reason. Same as `true`.
* `"NO"` disables capturing. Same as `false`.

Network dumps are captured as `pcap` files and printed hex-encoded to `STDOUT` at the end of the
test run. You can save the dump in hex format and then convert it to binary with
`xxd -r -p [hexdata]`.

### Environment

| Field            | Type                                    | Description                                                                                                                                                                                                                                                       |
|------------------|-----------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| name             | String                                  | environment's name, for debugging                                                                                                                                                                                                                                 |
| services         | Dictionary of [LaunchArgs](#launchargs) | Collection of services. Dictionary keys are the service's name and value contains launch information.                                                                                                                                                             |
| children         | Array of [Environment](#environment)    | Collection of child environments to spawn.                                                                                                                                                                                                                        |
| devices          | Array of String                         | Collection of endpoint names to attach to environment as VirtualDevices. Endpoints will be available under `/vdev/class/ethernet/[endpoint_name]` for every component launched by the cmx facet or with this environment's ManagedLauncher.                       |
| test             | Array of [LaunchArgs](#launchargs)      | Collection of test processes. A test process will have its exit code monitored. If any test fails, the sandbox exits immediately and copies the return code. Otherwise, the sandbox will only exit when ALL tests have exited successfully                        |
| apps             | Array of [LaunchArgs](#launchargs)      | Collection of applications to run. An application is spawned into the environment asynchronously and the sandbox doesn't care about its exit status.                                                                                                              |
| setup            | Array of [LaunchArgs](#launchargs)      | Collection of setup processes to run. Setup processes will run sequentially and synchronously (sandbox waits for it to exit **successfully**) before the tests are executed. Any *setup* process that fails will cause the sandbox to exit with a failure status. |
| inherit_services | Boolean                                 | Whether to inherit the parent environment's service configuration. Defaults to **true**.                                                                                                                                                                          |
| logger_options   | [LoggerOptions](#loggeroptions)         | Options for environment specific logger.
| guest            | Array of [Guest](#guest)                | Collection of guest VM's to create.

### LaunchArgs

*Note*: Launch args can always just be specified as a String value.
In that case, it's as if only the *url* field had been specified.

| Field     | Type            | Description                                                                                                                                                                       |
|-----------|-----------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| url       | String          | Fuchsia package URL to launch. Only valid package URLs will be accepted. If empty or not present, will default to the value of [Config](#config)'s `default_url`.                 |
| arguments | Array of String | Command-line arguments to pass to process.                                                                                                                                        |


### Network

| Field     | Type                          | Description                                                        |
|-----------|-------------------------------|--------------------------------------------------------------------|
| name      | String                        | **Required** network name identifier. Must be unique.              |
| endpoints | Array of [Endpoint](#endpoint) | Collection of endpoints to be created and attached to the network. |

### Endpoint

| Field | Type    | Description                                                                                               |
|-------|---------|-----------------------------------------------------------------------------------------------------------|
| name  | String  | **Required** endpoint name identifier. Must be unique in even across networks.                            |
| mac   | String  | MAC address of virtual endpoint. If not set will be generated randomly using the endpoint's name as seed. |
| mtu   | Number  | Endpoint's MTU. Defaults to **1500**.                                                                     |
| up    | Boolean | Toggle endpoint to link up as part of setup process. Defaults to **true**.                                |


### LoggerOptions

| Field         | Type                                        | Description                                      |
|---------------|---------------------------------------------|--------------------------------------------------|
| enabled       | Boolean                                     | Enable log output (default: true)                |
| klogs_enabled | Boolean                                     | Enable capturing kernel logs (default: false)    |
| filters       | [LoggerFilterOptions](#loggerfilteroptions) | Options for log filterering                      |

> Note that the `enabled` option in `LoggerOptions` does not affect the availability of the logger
> service on the environment, but rather just controls that the logs be printed to stdout.
>
> The `klogs_enabled` flag controls, in turn, the logger process that is launched in each
> environment making it listen or not to the kernel logs.


### LoggerFilterOptions

| Field         | Type                | Description                                                                     |
|---------------|---------------------|---------------------------------------------------------------------------------|
| verbosity     | Integer             | Log verbosity level (default: 0)                                                |
| tags          | Array of Strings    | Tags required to show log (empty array means no filters on tags) (default: [])  |


### Guest

| Field         | Type                  | Description                                             |
|---------------|-----------------------|---------------------------------------------------------|
| label         | String                | Name used to identify a specific VM instance.           |
| url           | String                | .cmx package that encapsulates guest binaries.          |
| files         | Dictionary of Strings | Maps input data file path to guest VM destination path. |
| networks      | Array of Strings      | Ethertap networks that guest should be connected to.    |

> Note that only one `guest` is allowed. Also, `networks` cannot contain more than a single entry.

## Helpers

`netemul_sandbox` provides helpers that can be launched in sandboxed environments to perform common
operations.

### netstack_cfg

The `netstack_cfg` helper is a CLI-like tool that uses the *NetworkContext* service to retrieve
emulated endpoints and attach them to netstack instances in an emulated environment. You can launch
it by using its package url: `fuchsia-pkg://fuchsia.com/netemul_sandbox#meta/helper_netstack_cfg.cmx`.
It receives the command line arguments shown below and is typically used as a "setup" process in an
[environment's facet definition](#environment).

```
netstack_cfg
Configure netstack from emulated environments.

USAGE:
    netstack_cfg [FLAGS] [OPTIONS] -e <endpoint>

FLAGS:
    -h, --help             Prints help information
        --skip-up-check    netstack_cfg will by default wait until it sees the interface be reported as "Up", skip-up-
                           check will override that behavior.
    -V, --version          Prints version information

OPTIONS:
    -e <endpoint>        Endpoint name to retrieve from network.EndpointManager
    -i <ip>              Static ip address to assign (don't forget /prefix termination). Omit to use DHCP.

```

### mock_device_settings

The `mock_device_settings` helper is a mock implementation of the FIDL protocol
`fuchsia.devicesettings.DeviceSettingsManager`. You can launch it
by using its package url: `fuchsia-pkg://fuchsia.com/netemul_sandbox#meta/mock_device_settings.cmx`.
It receives the command line arguments shown below and is intended to be used as a "services" entry in
[environment's facet definition](#environment). You **must** specify the values for **every** key that your
tests may want to access as a command-line option.


```
Serves DeviceSettingsManager with a memory-backed database

USAGE:
    mock_device_settings [OPTIONS]

FLAGS:
    -h, --help       Prints help information
    -V, --version    Prints version information

OPTIONS:
    -i <int_key>...           Starts with a pre-set integer key, format [k]=[v] e.g. Audio=100
    -s <string_key>...        Starts with a pre-set string key, format [k]=[v] e.g. DeviceName=my-device-name

```
