This directory contains services used by the current story runner
implementation. They are skeletal and will be revised.

What happens so far:

1. The story runner app exposes a service that allows to start a
   story. The running story is represented as a Session.

2. The Session service allows to request to run a mojo app.
   Invoking a mojo app through the Session service causes (2.1) the
   specified mojo app to be run, (2.2) the Module service of that
   app to be requested, and (2.3.) Initialize() method of the
   Module service to be invoked with a handle of the Session
   service instance as a parameter. Thus, the module so started is
   able to start more modules, all in the scope of the same
   Session.

3. The Session service also exposes a factory for instances of the
   Link service. A Link instance exposes an API to store and
   retrieve values, and to register callbacks to notify when values
   stored in the instance change. A Link instance is shared between
   each pair of requesting and requested modules in the session.

What is still missing:

4. The Module and Link instances in the session are planned to be
   recorded in the Ledger by the story runner.

5. An existing story can be restarted.

6. The Link instance holds data according to a Schema.

Other issues with these interfaces:

There is a trade off between Interface request arguments and
Interface return values. Interface requests are less verbose
because they are synchronous. Interface requests can be sent to
handles and the handles be passed on immediately. However, if
the receiving side is to make calls on the bound implementation
and delegate implementation to a further service (as for
Session::StartModule()), then this is possible only for a
returned interface.

