## How to run

*** promo
You must first configure your environment to prevent device_runner from starting
by default. See [Prerequisites](../../README.md#Prerequisities) in the Modular
README.
***

```
device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=suggest_shell_controller
```

## What is it

The story in this example contains two modules, `controller` written in C++ and
`view` written in flutter. The view displays a simple input for a text string,
which is just given to the controller through the link. The controller takes the
text string and forms a suggestion proposal from it, which it sends to the
suggestion service.

This simple example does a few things:

1. It exercises the suggestion machinery a little bit. It allows to make a
   suggestion from a module and deliver it to the suggestion machinery to see
   what happens. Because the suggestion originates in a module, this approach
   can be used to experiment with variants of UI that supports forming
   suggestions.

2. It explores a design pattern where the view is a child module to its
   controller module. Since the view module is displayed by the controller
   module, and the view state is given to the view module by the controller,
   what we used to think of as view state now becomes controller state, which is
   intriguing since usually nobody thinks of controllers as having state, let
   alone persistent one.

3. Demonstrates cross language cooperation of modules.

4. We introduce a generic view host that simply embeds view.
