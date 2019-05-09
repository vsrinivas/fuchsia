# Isolated Component Test

This is a test of the `create_nested_environment` and
`launch_component_in_nested_environment` function in `fuchsia_component`. Its
goal is to ensure the following:

* A nested environment is successfully created.
* The `add_proxy_service` function successfully proxies a loader for the new
  environment, allowing components to be launched.
* The `add_proxy_service_to` function successfully proxies a FIDL service
  implemented within the environment to other components within the environment.
* Components can be brought up in the nested environment.
* Components inside the nested environment only have access to the specific set
  of services granted by their parent, and cannot access other services within
  their parents' environment. Services added to the nested environment must also
  be visible to the components inside the nested environment.

The test ensures this by running four separate components.

* `fuchsia_component_test_outer_component` simulates the "outer world".
  It spawns `fuchsia_component_test_middle_sibling_component` which
  exposes an implementation of `EchoExposedBySibling` to the nested environment,
  which multiplies all inputs by 2.
  It then runs `fuchsia_component_test_middle_component` in the nested
  environment containing `EchoExposedByParent` and `EchoHiddenByParent`, each of
  which echos a number and returns it to the client.
* `fuchsia_component_test_middle_component` simulates a parent component attempting to
  create an isolated child. After confirming that it can successfully access
  `EchoExposedByParent`, `EchoExposedByParent`, and `EchoHiddenByParent`, it
  launches `fuchsia_component_test_inner_component` in a nested environment that
  only has access to `EchoExposedByParent`, which has been overridden to always
  return '42'.
* `fuchsia_component_test_inner_componnent` simulates the isolated child component.
  It confirms that it can't access `EchoHiddenByParent` or
  `EchoExposedBySibling`, and that `EchoExposedByParent` has been successfully
  overwritten, always returning '42'.

## Running the test

`fx run-test fuchsia_component_test_outer`
