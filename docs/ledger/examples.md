# Examples

List of example apps that directly integrate with Ledger - this does not include
apps that use Ledger through Framework fuchsia::modular::Link objects.

## C++

 * [clipboard] - system clipboard agent which stores state in Ledger
 * [Ledger benchmarks] - benchmarks that exercise Ledger APIs and capture
   performance traces
 * [story runner] - framework facility that stores the state of user stories in
   Ledger

## Dart

 * [chat] - a chat agent that uses Ledger to store messages
 * [contacts] - Ledger-based contacts agent
 * [todo list] - a todo-list module that uses Ledger to store the todo items
   * this is a minimal working example of a Ledger-based app

[chat]: https://fuchsia.googlesource.com/topaz/+/master/app/chat/
[clipboard]: /bin/agents/clipboard/
[contacts]: https://fuchsia.googlesource.com/topaz/+/master/app/contacts/
[Ledger benchmarks]: /bin/ledger/tests/benchmark
[story runner]: /bin/user_runner/story_runner/
[todo list]: https://fuchsia.googlesource.com/topaz/+/master/examples/ledger/todo_list/
