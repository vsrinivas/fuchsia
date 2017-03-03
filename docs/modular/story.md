Story
=====

A story is a logical container for a root application along with associated
data. An instance of a story can be created, deleted, started and stopped by the
system in response to user actions. Creating a new story instance creates an
entry in the user's ledger which stores the data associated with this story
instance; deleting a story instance deletes the associated data. Starting a
story instance runs the root application; which may start other applications. If
the root application is a module, it can start other modules and access / modify
data associated with the story instance (via links). The root application must
also implement the view associated with this story which might embed views from
other applications / modules.

## See also:
[StoryProvider](../services/story/story_provider.fidl)
[StoryController](../services/story/story_controller.fidl)
