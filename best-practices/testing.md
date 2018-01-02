# Testing best practices

## How to know whether a test is useful

A test's primary purpose is to help people understand the behavior of the code
being tested. If a test gives you some insight into how Fuchsia behaves, it's
useful. If it doesn't, you should think about what insight you want, and find a
way to write a test that gives you that insight. If there's nothing in
particular you want to learn from the test, you should delete it.

Because tests' primary purpose is to help human understanding, they should be
written with a greater emphasis on readability: more explicit and flatter, with
fewer branches and optimizations.

Human working memory is severely limited, and people can't reason about things
that they can't hold in memory. So your goal should be to require readers of
your code to hold very few things in in memory in order to understand what the
tests are saying.

## What an ideal test looks like

This is an ideal test:

```cpp
EXPECT_EQ(2, HowManyThrees(3003));
```

Why is it ideal? Basically, because it's declarative. It shows you a pair of
values, `(3003, 2)`, and it tells you that the function `HowManyThrees` relates
the two values in the pair. We could have more test cases just like it, to tell
us more about the function:

```cpp
EXPECT_EQ(0, HowManyThrees(222);
EXPECT_EQ(1, HowManyThrees(3));
EXPECT_EQ(2, HowManyThrees(-33);
```

In each case, we have to do very little mental work to understand the new
information. The lack of procedural steps means we don't have to follow along
with the computer as it works through the test, which means we can't
accidentally do a step wrong in our heads.

Here's a non-ideal way to test the same function:

```cpp
for (int i = 0; i < 100; i++) {
  int ones_digit_is_three = (i % 10 == 3);
  int tens_digit_is_three = (i >= 30 && i <= 39);
  EXPECT_EQ(ones_digit_is_three + tens_digit_is_three, HowManyThrees(i));
}
```

Why isn't this ideal? It does give us some information about the behavior of
`HowManyThrees`. But that information is obscured by the procedural details of
the test. To get any insight from the test, we have to mentally walk through
all of the steps. A few of those steps are probably implemented in
`HowManyThrees` itself, which means we could have gotten similar information by
just reading the implementation code, rather than writing a test for it.

The looping test has a common problem: it's designed to be _comprehensive_
instead of _simple_. The implementation of `HowManyThrees` is already a
comprehensive statement of how it behaves, so we don't actually need another
one. What we need is a simpler way to say something about its behavior,
something that we can easily fit in our heads. That's what's so nice about the
ideal test: it shows us just a small amount of information, small enough for us
to easily reason about.

## Case study: How to get a test closer to the ideal

The `HowManyThrees` example is very simple. The code we test in Fuchsia is much
more complex and difficult to test. But the point of having an ideal test isn't
that all of our tests should look like it. Rather, the ideal is what we compare
the tests to as we improve them.

Since the ideal test is just an input-output pair associated with a function, we
can improve other tests by trying to model them similarly: as input-output pairs
for a function. In reality, the code we're working with doesn't look anything
like a pure function with easily identifiable inputs and outputs, but we can
analyze it that way by asking the following questions:

1. What pieces of information are the **inputs** to the function? We can think
   of inputs as our independent variables, meaning they can change
   independently of the code under test. They might be:
   * The initial state of objects used in the test.
   * State changes done by the test code in between invocations of the code
     under test, e.g., resetting objects or sending additional messages.
   * Data retrieved from the network, or from a FIDL service not controlled by
     the test.
   * Random numbers generated during test execution.
   * Timing variables, such as the overall time it takes for the CPU to execute
     some code, the relative execution time of code running on more than one
     thread, or the current time from the system clock.
2. What pieces of information are the **outputs** from the function? These might
   be return values from functions, but often they are state that has mutated
   during the test.
3. What code comprises the **function** itself? This is any code that helps in
   the transformation of the test's input into its output. The function is often
   comprised of code across several classes, including utility libraries not
   directly related to the code we care about testing.

Let's walk through these questions with a real example. One of the Ledger unit
tests, `PageStorageTest.CreateJournals`, was flaky. It was occasionally failing
on this line:

```cpp
storage_->CommitJournal(
    std::move(journal),
    callback::Capture(MakeQuitTask(), &status, &commit));
EXPECT_FALSE(RunLoopWithTimeout());
```

In this case, the **output** that indicated a failing test was the return value
of `RunLoopWithTimeout`, which was true when it was supposed to be false.

Since we're modeling the test as a set of input-output pairs describing a
function, a test flake means one or more of the **input** values was different
across different test executions. This is true of nearly every test flake. Even
when it seems like the inputs are exactly the same, there's usually a hidden or
implicit input somewhere.

Where is this hidden input? We can find it with an iterative process:
start at the failure and repeatedly ask, "Why is the behavior different this
time?" The failure was caused by the return value of `RunLoopWithTimeout`, so we
need to look at its implementation. Here's a simplified version:

```cpp
fxl::TimeDelta timeout = fxl::TimeDelta::FromSeconds(1);
bool timed_out = false;
message_loop_->task_runner()->PostDelayedTask(
    [message_loop_, &timed_out] {
      timed_out = true;
      message_loop_->QuitNow();
    },
    timeout);
message_loop_->Run();
return timed_out;
```

Why would this sometimes return true? Well, something timed out. Specifically,
it took longer than one second for the callback from `CommitJournal` to execute,
which would have quit the message loop and prevented the delayed task from
getting executed, which would have prevented `timed_out` from getting set to
true.

The crucial thing to notice here is that the passing/failing condition here
isn't actually caused by our **function**. It isn't caused by the behavior of
`PageStorage` or any of its neighboring classes. Rather, it's caused by the
execution environment. Sometimes the CPU is executing our code fast enough for
the callback to be reached before the timeout, and sometimes it isn't. This
condition, whether the code is executing fast enough, is an **input** to the
function.

To de-flake the test, we want to somehow control this input. One way to control
an input is to explicitly set it to a known value in the test. This is a useful
strategy if the input is a random number or data from the network, but in this
case we can't use that strategy, because we need a real CPU to run the test, and
we can't control how fast it runs.

But we can use another strategy, which is to reshape our **function**, the
system being tested, so that it no longer takes this timing value as an input.

Here's how we do it. The `PageStorage` instance uses `TaskRunner` as a
dependency all over the place: it passes it to child objects and they use it to
execute tasks asynchronously. But we don't need to use the `MessageLoop`
implementation of `TaskRunner` to do that. We can write a much simpler
implementation for the purpose of testing. Here's what it looks like:

```cpp
class FakeTaskRunner : public fxl::TaskRunner {
 public:
  void PostTask(fxl::Closure task) {
    task_queue_.push(std::move(task));
  }

  void Run() {
    while (!task_queue_.empty()) {
      fxl::Closure task = std::move(task_queue_.front());
      task_queue_.pop();
      task();
    }
  }

 private:
  std::queue<fxl::Closure> task_queue_;
};
```

With `FakeTaskRunner`, we have gained insight into what happens to the tasks
when they get posted. We now know exactly which tasks were requested, so we can
run the tasks until there are none left. This means we no longer need to hand
off control to the blocking `MessageLoop::Run` function, which means we no
longer need to race a timer against it, which means the changing input value of
"whether the CPU finishes its work in time" can be eliminated.

After [the change](https://fuchsia-review.googlesource.com/c/peridot/+/93875),
the tests still don't look anything like the simplified ideal test, but they
have moved slightly in that direction.

When evaluating a test improvement, as when evaluating the usefulness of a test,
we want to ask how the test helps us understand the code. The purpose of
`PageStorageTest` is to help developers understand the behavior of
`PageStorage`. In that sense, the change was a helpful one, because it reduced
the scope of the test to basically just `PageStorage` and its child classes,
rather than including all of the things `MessageLoop` does.

`PageStorageTest` is now more self-contained, with simpler dependencies and no
global state. It's not a coincidence that this simplification also eliminated
a flake risk: flakiness is usually caused by _hidden_ inputs that change.
Rearranging the code to make inputs explicit can bring those hidden behaviors to
light.
