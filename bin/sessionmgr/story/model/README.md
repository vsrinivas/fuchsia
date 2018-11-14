### StoryModel

The `StoryModel` FIDL table is used to represent the state of a story.
`sessionmgr` keeps a separate `StoryModel` in memory for each running story,
and also persists changes to it onto storage.  Mutations to `StoryModel` are
done using the `StoryMutator` interface and changes from these mutations
are notified to registered observers (using the `StoryObserver`
interface).

This directory defines classes that define the control flow for mutating a
`StoryModel` FIDL table.

The following interfaces are defined:

* `StoryMutator`: Allows clients to issue a series of `StoryModelMutation`
  structs that describe mutations to a `StoryModel`.
* `StoryObserver`: Used to ready `StoryModel` and observe changes to it.
  Changes to `StoryModel` may happen through `StoryMutator` or, in the
  case of a distributed storage implementation such as Ledger, from sync'ing
  with peers.
interface. Allows clients to read the current `StoryModel` state, and register
for updates when the `StoryModel` changes.

Both of the above are abstract base classes to aid in testing: clients that
wish to mutate and/or observe a `StoryModel` will accept a `StoryMutator`
or `StoryObserver` as a constructor argument.  Making them abstract allows
injection of test implementations that do not require the full machinery
introduced by `StoryModelOwner`.

#### `StoryModelOwner` class

`StoryModelOwner` is has its own implementations of `StoryMutator` and
`StoryObserver`, and coordinates the flow of data from
`StoryMutators` through a `StoryModelStorage`, applies those changes
to a `StoryModel` and flows this new model to `StoryObserver` instances.
It acts as a factory for its own implementations of `StoryMutators` and
`StoryObservers`.

#### `StoryModelStorage` interface

Supplied to `StoryModelOwner` at the time of its creation, a
`StoryModelStorage` is responsible for consuming mutation commands and
updating its persistent storage layer by applying a set of mutation commands,
as well as notifying of mutations that have been applied. A request to mutate
does not necessary result in those exact mutations being observed (in the case
of conflict resolution), nor do observed mutations imply a request was made (in
the case of a `StoryModel` backed by distributed storage).

A "no-op" StoryModelStorage (one that does not result in any disk-backed
or other durable storage) would immediately notify of any incoming mutations
without applying them anywhere. In this case the `StoryModel` would be resident
in memory only.

#### Flow of Control

Mutation commands flow from anything that has a `StoryMutator` through a
`StoryModelOwner`, `StoryModelStorage` and are translated into new
`StoryModels`. From that point forward, observers see the new `StoryModel`
values.

```
[some system] -> StoryMutator   ->  StoryModelOwner         |  commands
                                                  |              |  "
                                       StoryModelStorage    |  "
                                                  |              |  "
[other system] <- StoryObserver <-  StoryModelOwner         $  model
```

### Example

The constructor for your average System that both mutates and observes a
`StoryModel` would look like:
```
class Foo : public System {
 public:
  Foo(std::unique_ptr<StoryMutator> mutator,
      std::unique_ptr<StoryObserver> observer, ...);
};
```

In production, we will create this by leveraging the `StoryModelOwner` to
create those dependencies:

```
// assume StoryModelOwner has already been defined.
auto foo_system = std::make_unique<Foo>(owner->NewMutator(), owner->NewObserver(), ...);
```

For testing, we leverage test versions of both:
```
auto test_mutator = std::make_unique<TestStoryMutator>();
auto test_observer = std::make_unique<TestStoryObserver>();

// Retain pointers to both |test_mutator| and |test_observer| so we can trigger
// behavior and validate side-effects.
auto mutator_ptr = test_mutator.get();
auto observer_ptr = test_observer.get();
auto foo_system = std::make_unique<Foo>(std::move(test_mutator), std::move(test_observer));

observer_ptr->NotifyOfModel(new_model);  // Push a new model to observers!
// |foo_system| should have generated new mutations as a side-effect.
EXPECT_TRUE(3, mutator_ptr->GetNumCommandsIssued());
```