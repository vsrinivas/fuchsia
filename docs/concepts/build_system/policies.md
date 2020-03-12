# Fuchsia build policies

The Fuchsia build is a large and complicated build, as it covers a wide
variety of software and dependencies, both first and third party, from kernel
up to multi-media user applications and whole-system constructions.

The Fuchsia build aims to provide common desirable build properties:

* Hermeticity - the property that the build is self-contained and neither
  influences external software and configuration or is influenced by external
  software and configuration.
* Repeatability and reproducibility - the property that two builds from the same
  source tree produce the same output. This property is desirable for security
  and auditing, as well as determinism in the engineering process.
* Efficient - builds should only spend time doing work relevant to the build,
  and must aim to minimize the impact on both human and infrastructure costs.

## Only use the dependency graph to modulate runtime behavior

It is a goal of the Fuchsia architecture that all products may co-exist in a
global Fuchsia ecosystem, sharing component addressability and updates
throughout the ecosystem. In order to reach this goal, components that are
provided a name in that ecosystem must have the same meaning regardless of
the particulars of the build from which they are produced. If a build may
produce a package called `fortune`, there should not be any build
configuration used to produce `fortune` that alters the intended use case or
behavior of fortune that is not considered a bug or a local-only workflow
tool.

Examples of allowed configuration changes for a build target:

- Local debugging flags that are never built in "production" releases, such
  as DEBUG_ASSERT, or increased logging levels.
- ASAN, TSAN, and other sanitizer builds that are not inputs to production
  components, but are used in development environments for validation.

Examples of disallowed changes for a build target:

- Enabling or disabling feature sets, for example a build flag that modulates
  whether or not component `fortune` implements the `com.fuchsia.FortuneExtra`
  interface.
- This policy includes hardware feature modulation - in order to provide for
  example a driver that has build support to disable a feature would be built
  twice, once with the feature enabled, and once without and packaged into
  two package targets with distinct names.

It must be possible for one configuration and invocation to build all
possible feature axes and combinations. Products and image sets are composed
with particular features by modulating the set of dependencies those products
are composed from, rather than modulating the behavior or implementation of a
particular component.

The following is a small example demonstrating the approach:

``` gn
config("feature-foo") {
  cflags = [ "-DFOO=1" ]
}

executable("mytool-nofoo") {
  ...
}

executable("mytool-foo") {
  configs = [":feature-foo"]
}

package("mytool-with-foo") {
  deps = [":mytool-foo"]
  ...
}

package("mytool") {
  deps = [":mytool-nofoo"]
  ...
}
```

An example of an undesirable approach is as follows:

``` gn
declare_args() {
  enable_foo = false
}

executable("mytool") {
  if (enable_foo) {
    cflags = [ "-DFOO=1" ]
  }
  ...
}
```

The above negative example would modulate the behavior, and definition of the
component "mytool" based on build configuration, preventing the build from
being able to build all possible configurations, and violating the component
addressability scheme of the Fuchsia architecture.

## Board vs. product axes

It is often tempting to want to use specific board properties, or a board
name to modulate the configuration or behavior of software. This is
undesirable as products should run on a variety of target hardware. A board
does not define a product, even if it might for a user. Instead, a board
defines only a set of hardware properties.

Board package dependency configuration should ideally only provide
configuration data and driver selection. Runtime or product software should
not be reconfigured by the board, as the board as an abstraction cannot know
what software configuration might run on it. For example, you may run the
router product configuration on top of a VIM2 board configuration.

## Package configuration vs. config_data

Packages often contain components with runtime configurable behavior and/or a
need for additional data or metadata that a component consumes in order to
function for a particular purpose. There are two commonly used methods to
provide such data to a component, first to include that data in variations of
the package, for example `fortune-with-jokes` and `fortune-without-jokes`,
and the second to configure `fortune` using `config-data` provided data. The
package configuration pattern is always preferred over the config-data
mechanism where applicable, as it provides the maximum flexibility and
longevity in the build and product composition system. Additionally, the
config-data pattern is to be considered temporary - it does not yet solve the
configuration management challenge well for all possible forms of product
composition, and may not last through future architecture changes.

## Null builds

A build that invoked twice with no source code changes must perform no build
actions. The ninja build system that is used in Fuchsia encodes a strong
concept of a null build that is performed when ninja can observe no dirty
inputs to any targets. A failure of a build configuration to be a null-build
on repeated invocations is an indication of a configuration error in the
build system that should be fixed promptly. Failures to null-build often
indicate failures of hermetic build behavior or failures in repeatability of
the build.

## Known Bugs and Exceptions

The following exceptions are recorded as known issues that may not be
resolved for an extended time. Newly discovered issues of a similar issues
are to be first treated as bugs rather than becoming new exceptions.

- The following packages are in violation of the package specialism and
  naming rules: system_image, config-data, shell-commands, update.
- If a build is reconfigured (for example by `fx set`) to include a subset of
  the previously requested artifacts, some dependencies not relevant to the new
  configuration may be rebuilt, and sometimes this may lead to build failure.
  This is a known challenge of the build system and users are encouraged to
  clean their build tree when performing major or subtractive build
  configuration changes.
- Currently $PATH is consumed as-is by the build due to the lack of a
  prebuilt Python distribution and dependence on a handful of other utilities
  common on Unix type systems.
