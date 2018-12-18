## Intents

An `Intent` is used to instruct a module to perform an action. The intent
contains the action name, the arguments for the action parameters, and an
optional handler which explicitly specifies which module is meant to perform the
action.

### Declaring Actions

Modules can declare which actions they handle, and their associated parameters
in their [module facet](module_facet.md). The modular framework will then index
the module and treat it as a candidate for any incoming intents which contain
the specified action, and don't have an explicit handler set.

### Handling Intents

When an intent is resolved the framework determines which module instance will
handle it.

The framework then connects to the module's `fuchsia::modular::IntentHandler`
service, and calls `HandleIntent()`. The framework will connect to the intent
handler interface each time a new intent is seen for a particular module
instance, and intents can be sent to modules which are already running. Modules
are expected to handle the transition between different intents gracefully.

### Example

To illustrate the intended use of intents, consider the following fictional
example: a new story has been created with a module displaying a list of
restaurants and the module wants to show directions to the currently selected
restaurant.

The restaurant module creates an intent with a `com.fuchsia.navigate` action
with two parameters `start` and `end`, both of type `com.fuchsia.geolocation`
and passes it to the modular framework via `ModuleContext.AddModuleToStory`.

At this point, the framework will search for a module which has declared support
for the `com.fuchsia.navigate` action. Once such a module is found, it is added
to the story and started. The framework then connects to the started module's
`IntentHandler` service and provides it with the intent.

At this point, the restaurant list module's selected restaurant changes. It
again creates an intent, with the same action as before but with new location
arguments and calls `ModuleContext.AddModuleToStory`.

The framework now knows there is already a module instance running (in this case
`AddModuleToStory` uses the `name` parameter to identify module instances)o
explicitly specify a which can handle the given action, and connects to its
`IntentHandler` interface and provides it with the new intent. The navigation
module can then update its UI to display directions to the new restaurant.

If the restaurant module wanted a specific module (e.g. `Fuchsia Maps`) to
handle the intent, it would set the `Intent.handler` to the component URL for
the module.
