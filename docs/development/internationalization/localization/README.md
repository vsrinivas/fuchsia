# Localization

[Localization](https://en.wikipedia.org/wiki/Language_localisation) (L10N for
short) is a process for adapting the software so that it is usable in a
particular region; or a number of regions. Fuchsia has a workflow that allows
program authors to equip their programs with language-specific resources
(a.k.a _localized assets_).

At present, Fuchsia's localization support is limited, when compared to the
scope of all localization features out there. However, even in its limited
scope (though the scope will grow) enough small scale decisions have been made
that it is useful to document them in the form of a specification.

This specification is by no means complete or final. We reserve the right to
modify it in the future, and though we will make a best effort to evolve it in
backward compatible ways, there may be cases in which breaking changes could be
introduced if the benefits outweigh the potential downsides.

## Message Translation

The first functionality that Fuchsia's localization provides is message
translation.  Conceptually this gives a program the ability to display
user-facing messages in the user's language of choice.  This is achieved using
locale-sensitive formatter printing.  The program needs to keep the
"localization context" where user's localization preferences are stored.  When,
in the course of program execution, there comes a point that a message "Hello
world!" needs to be displayed on screen in the user's native language (for
example Spanish would be encoded as "es", for European Spanish, and "es-419"
for Spanish as used in the Americas), the program can look up a translation by
providing an abstract `[Lookup]` service with the original message and the
desired translation.  Conceptually this will amount to a line in the code that
matches this general pattern:


```
[Lookup].String({locale-ids=["es-419"]}, MSG_Hello_World) ⇒ (yields the translation)
```

In the above example, `[Lookup]` can be any sort of callable endpoint: it could
be a library exposed function, or could be an interface point for an
[RPC](https://en.wikipedia.org/wiki/Remote_procedure_call) stub that fetches
the translation over the network.  The abstract operation of "fetching a
translated message" is here called `String` to distinguish it from other
possible calls to typed data, such as `StringArray` or others.

Note, messages can get quite a bit more elaborate than that, which is why we
typically don't want the program authors to handle them directly, but rather
through message IDs.

Two more things are of note:

1.  The language identifiers are specified as [Unicode locale
	IDs](http://unicode.org/reports/tr35/#Unicode_locale_identifier) (hence the
	named parameter `locale-ids` in the example), and multiple such locale IDs
	can be provided at once.  This is because users may have more than a single
	preferred language, and may have a hierarchy of languages by preference.
	This allows the localization system to choose the best available message,
	possibly in more than a single language in a single session.
1.  The messages are not specified as their string representation in code.
	Rather, they are referred to by a unique message identifier. In the example
	above, this was arbitrarily named `MSG_Hello_World`.  While schools of
	thought differ on whether strings should be internalized or externalized,
	we opted for the latter.  Our main reasons were to keep the source code
	free from linguistic concerns, which makes the translation toolchain
	somewhat easier to maintain, and makes translations at scale easier to
	manage.

Two main questions arise from the example above:

1.  What does the _concrete_ interface to the `[Lookup]` service look like in
	the programmer's language of choice? And,
1.  How do translations make their way to my program so that they are available
	to use?

We will answer these in turn.

## Lookup API

The Lookup API library is used to obtain translated strings.  A simplified view
of the  Lookup API in C++ is as follows:

```cpp
class Lookup {
public:
  enum class Status {
    // No error.
    OK = 0,
    // The resource was unavailable as requested.
    UNAVAILABLE = 1,
  };
  static fit::result<std::unique_ptr<Lookup>, Lookup::Status>
    New(const std::vector<std::string>& locale_ids);
  fit::result<std::string_view, Lookup::Status> String(uint64_t message_id);
};
```

The actual API can be seen in the file
<code>[lookup.h](/src/lib/intl/lookup/cpp/lookup.h)</code>,
and is essentially the same as shown above, except that it contains
documentation, construction and testing overhead.  At the time of this writing,
only a high level C++ API is available for use.  We will be adding high level
APIs in other languages as need arises.  A [low-level C
API](/src/lib/intl/lookup/rust/lookup.h)
is available as a basis for writing [FFI
bindings](https://en.wikipedia.org/wiki/Foreign_function_interface) to this
functionality in other languages.  As a special case,
[rust](https://www.rust-lang.org/) does not need the FFI bindings since the
low-level implementation is in rust and can be interfaced with directly; but an
actual rust API has not been formulated yet.

A basic usage of the Lookup API looks like this:


```cpp
std::vector<std::string> locale_ids = {"nl-NL"};
auto result = Lookup::New(locale_ids);
if (result.is_error()) {
  return;
}
auto lookup = result.value();
auto lookup_result = lookup.string(42);
if (lookup_result.is_error()) {
  // handle error
  return;
}
std::string_view message = lookup_result.value();
// Use `message`.
```

The example is taken from the [`lookup.h`
documentation](/src/lib/intl/lookup/cpp/lookup.h#10). Knowing the API, this
example is fairly straightforward, save for one thing: the call
<code>lookup.string(...)</code> uses a magic number <code>42</code> to look up
a message.  It is fair of you as a programmer to ask where this number comes
from.  The next section addresses this question.


## Localization workflow

Since it is impractical to request of a programmer to know by heart a magic
key referring to a specific message, it stands to reason that the localization
system should provide an ergonomic way to refer to these keys in a symbolic
manner (remember the abstract example for `MSG_Hello_World` above.

Following the best practices for 18n and l10n, the source strings live in an
XML file (here, named `strings.xml`), explained below. An example of a
`strings.xml` file is shown below. The goal of this file is to declare all
externalized strings that our program uses, and give them a locally unique
`name`.  The strings will be used as a basis for translation, and `name`s will
be used as a basis for the symbolic constants used to refer to particular
messages.

```xml
<!-- comment -->
<?xml version="1.0" encoding="utf-8"?>
<resources xmlns:xliff="urn:oasis:names:tc:xliff:document:1.2">
  <!-- comment -->
  <string
    name="STRING_NAME"
      >text_string</string>
  <string
    name="STRING_NAME_2"
      >text_string_2</string>
  <string
    name="STRING_NAME_3"
      >string with
an intervening newline</string>
</resources>
```

The file `strings.xml` goes through a series of transformations in which
language-specific varieties of the same file are produced by translators.  The
input-output behavior of the translation process is: `strings.xml` file goes
in, with strings written in some source (human) language, and multiple flavors
of `strings.xml` come out, each translated in a particular single language.
The entire translation process can be quite involved, in a large organization
it can involve farming tasks out to translators who may live around the world,
and scores of dedicated translation tooling. but the precise mechanics of the
box does not matter too much to us as consumers as long as the input-output
behavior of the process is upheld, and we're generally aware that the
translation could take a while.  The resulting files are converted into a
machine-readable form, and shipped alongside a Fuchsia program within the same
[Fuchsia package](/docs/glossary.md#fuchsia-package).
An important feature of Fuchsia packages is that they are inherently not an
archive, but rather a manifest that points to files by their content hash.  So
multiple programs can share the same files, and languages closely related
("en-US", "en-GB") can potentially share message disk space. The following
diagram shows a compact overview of the lifecycle of strings.

![The above image shows the localization flow. Since XML files are annotated they are not directly suitable for machine translation, so we convert to JSON files, for which we can reuse available libraries to load them, and construct a map from a key to message string.  These strings can then be used as format strings in `MessageFormat`.](images/localization-workflow.png "The above image shows the localization flow. Since XML files are annotated they are not directly suitable for machine translation, so we convert to JSON files, for which we can reuse available libraries to load them, and construct a map from a key to message string.  These strings can then be used as format strings in `MessageFormat`.")

### `strings.xml`

We are reusing the Android string resources XML format to represent localizable
strings. Since we will be adding nothing to the strings.xml format, the full
discussion of the features is delegated to the [string resources
page](https://developer.android.com/guide/topics/resources/string-resource).

While all that XML in the above diagram makes this discussion look like it just
emerged from some wormhole connected straight to the 1990s, XML is actually a
very good fit to describe annotated text. strings.xml is a format that has been
time-tested in Android so we know it will be adequate, and developers are
familiar with it.

For example, a string resource can be declared with annotations interleaved
into the source text.

```xml
<!-- … -->
<string name="title"
   >Best practices for <annotation font="title_emphasis">text</annotation> look like so</string>
<!-- … -->
```
Above: An example interleaving of translation text and annotation._

It is possible to interleave text that should be _protected from translation_,
[like so](https://developer.android.com/guide/topics/resources/localization):


```xml
<string name="countdown">
  <xliff:g id="time" example="5 days"
    >{1}</xliff:g> until holiday</string>
```

Above:: An example of an interleaving of a fenced-off parameter, annotated with
an example value and guarded with a tag that is not part of the string
resources data schema._

We can also define our own additions to the data schema if we so need, and
interleave that data schema _transparently_ in an existing schema.

There are some necessary constraints on the contents of the file above:

*   Every `name` attribute in the file must be unique.
*   Name identifiers may contain uppercase and lowercase ASCII letters, digits and underscores, but may not start with a number.  So for example,` _H_e_L_L_o_wo_1_rld` is allowed, but `0cool` is not.
*   No two `name`-`message` combinations in the file may repeat.

For the time being there are no provisions for having multiple strings files in
a project.

### Message identifiers

The message identifiers (the "magical" numeric constants for each message) are
generated based on the contents of the `strings.xml` file.  Every string
message gets a unique identifier, which is computed based on the one-way hash
on `name` and the contents of the message itself.  This identifier assignment
ensures that it is vanishingly unlikely for two different messages to
accidentally have the same resulting identifier.

The generation of these messages is automated by
[GN](https://gn.googlesource.com/gn/) build rules in Fuchsia, but is ultimately
performed by a program called
[strings_to_fidl](/src/intl/strings_to_fidl/README.md).
This program generates FIDL intermediate representation for the message IDs,
and the regular FIDL toolchain is used to produce language-specific versions of
that info.  As an example, the C++ flavor would be a header file with the
following content:


```cpp
namespace fuchsia {
namespace intl {

namespace l10n {
enum class MessageIds : uint64_t {
  STRING_NAME = 42u,
  STRING_NAME_2 = 43u,
  STRING_NAME_3 = 44u,
};

}  // namespace l10n
}  // namespace intl
}  // namespace fuchsia
```

The precise values assigned to each particular enum value in the example above
are not relevant.  The generation method is also not relevant at this time,
since all identifiers are generated at compile time and there is no opportunity
for version skew.  We may for now safely assume that an identical name-content
combination will _always_ have the same message ID assigned.

It is fairly easy to include the resulting file into a C++ program.  A minimal
example is given below, but refer to the fully worked-out example for the
precise details of the wire-up.  The library parameter `fuchsia.intl.l10n` is
provided directly by the author as a flag to `strings_to_fidl`; or if the
appropriate GN template is used, as a parameter to the GN template.

```cpp
#include <iostream>

// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"

// Each library name segment between dots gets its own nested namespace in
// the generated C++ code.
using fuchsia::intl::l10n::MessageIds;

int main() {
  std::cout << "Constant: " << static_cast<uint64_t>(MessageIds::STRING_NAME) << std::endl;
  return 0;
}
```

### \*.json

The FIDL and C++ code generation makes the message IDs available to the program
authors.  On the packaging side, we also must provide the localized asset for
each language we support.  At present the encoding for this information is
JSON.  This was done for expedience, but a number of improvements can be
made on that decision to improve performance and security.

Generating this information is delegated to the program named
[strings_to_json](/src/intl/strings_to_json/README.md),
which merges the original `strings.xml` with a language specific
file (for example, a French translation lives in `strings_fr.xml`).
Again, for builds driven by GN, the invocation of `strings_to_json`
is encapsulated in a build rule.

Example contents of a generated JSON file are given below.


```json
{
  "locale_id": "fr",
  "source_locale_id": "en-US",
  "num_messages": 3,
  "messages": {
    "42": "le string",
    "43": "le string 2",
    "44": "le string\nwith intervening newline"
  }
}
```


The JSON format has the following fields currently defined.  In case the table
below goes out of date, the source of truth for the JSON structure is the
[strings
model](/src/lib/intl/strings/src/json.rs#47).


| **Field** | **Type** | **Description** |
|-----------|----------|-----------------|
`locale_id` | Locale ID (string) | The locale for which the messages are translated. |
`source_locale_id` | Locale ID (string) | The locale of the source message file. |
`num_messages` | Positive integer | The number of messages that were present in the *original* `strings.xml`.  This allows us to estimate quickly the quality of the translation by comparing that number of messages with the number of messages that are present in the JSON file. |
`messages` | Map `[u64->string]` | A map from message ID to the appropriate message. |

## Packaging

The generated JSON file from the previous section must be bundled together with
the Fuchsia program so that it can be found at program runtime.  This is done
by the regular Fuchsia build rules, such as those in
[package.gni](/build/package.gni).

We have established some conventions for packaging resources (i.e. localized
assets). The schema is intended to be extensible to other asset types, and also
to be able to support _combinations _ of asset types which are sometimes useful
to have when expressing more complex relationships between device and locale (a
Hebrew icon version for a 200dpi display).  All paths below are relative to the
package's data directory and are found under `/pkg/data` on a running system.

| **Path** | **Description** |
|----------|-----------------|
| `assets/` | Stores all assets.  This is similar to how the <code>meta/</code> directory contains package manifests and other metadata.  In the future, this directory could contain conventional indices. |
| `assets/locales` | Stores data specifically for locales |
| `assets/locales/fr-fr` | Stores data for particular locales.  The locale names are individual directories in [BCP47](https://tools.ietf.org/html/bcp47) format. Each program contributes a single JSON file to this directory, named `program.json`, where the `program` part of the name is chosen by the author. We will, at some point, probably need to ensure that package and library names for files here do not collide. Also, due to Fuchsia's packaging strategy, it may pay to have many smaller files storing translations instead of one large one, for ease of update. |

