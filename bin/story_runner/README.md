This directory contains implementations of services used by the current story
runner implementation. They will be revised.

What happens so far:

1. The story runner app exposes the StoryRunner service that will start a story.
   The running story is represented as a Story.

2. The Story service allows to request to run an application. Invoking an
   application through the Story service causes (2.1) the specified application
   to be run, (2.2) the Module service of that application to be requested, and
   (2.3.) Initialize() method of the Module service to be invoked with a handle
   of the Story service instance as a parameter (among others). Thus, the module
   so started is able to start more modules, all in the scope of the same
   Story.

3. The Story service also exposes a factory for instances of the Link service. A
   Link instance exposes an API to store and retrieve values, and to register
   callbacks to notify when values stored in the instance change. A Link
   instance is shared between each pair of requesting and requested modules in
   the story.

4. The Module and Link instances in the story are recorded in the Ledger by the
   story runner.

5. An existing story can be restarted.

What is still missing:

6. The Link instance holds data according to a Schema.

Miscellaneous observations on these interfaces:

There is a trade off between Interface request arguments and Interface return
values. Interface requests are less verbose because they are synchronous.
Interface requests can be sent to handles and the handles be passed on
immediately. However, if the receiving side is to make calls on the bound
implementation and delegate implementation to a further service, then this is
possible only for a returned interface.

