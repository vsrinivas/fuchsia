# mdlint

`mdlint` is a Markdown linter. It is designed to enforce specific rules about
how Markdown is to be written in the Fuchsia Source Tree. This linter is
designed to parse [Hoedown](https://github.com/hoedown/hoedown) syntax, as used
on the [fuchsia.dev site](http://fuchsia.dev).

## Using mdlint

Configure, and build

    fx set core.x64 --with //tools/mdlint:host # or similar
    fx build

Example invocation running specific rules over `//docs`, and reporting all
findings:

    fx mdlint --root-dir docs \
              --enable no-extra-space-on-right \
              --enable casing-of-anchors \
              --enable bad-lists \
              --enable verify-internal-links

Example invocation running all rules over `//docs`, and only reporting findings
within Markdown documents whose filenames match `docs/contribute/governance`:

    fx mdlint --root-dir docs \
              --enable all \
              --filter-filenames docs/contribute/governance

## Testing

Configure

    fx set core.x64 --with //tools/mdlint:tests # or similar

Then test

    fx test mdlint_core_tests
    fx test mdlint_rules_tests

## Implementation

The linter parses Markdown files successively, typically all files under a root
directory.

Each Markdown file is read as a stream of UTF-8 characters (runes), which is
then [tokenized](#tokenization) into a stream of tokens. We recognize specific
patterns from this token stream, giving rise to a stream of patterns. This
layered processing is similar to how streaming XML parsers are structured, and
offers hook points for [linting rules](#linting-rules) to operate at various
levels of abstraction.

### Tokenization {#tokenization}

Because Markdown attaches important meaning to whitespace characters (e.g.
leading space to form a list element), and certain constructs' meaning depend on
their context (e.g. links, or section headers), the tokenization differs
slightly from what is typically done for more standard programming languages.

Tokenization segments streams of runes into meaningful chunks, named tokens.

All whitespace runes are considered tokens, and are preserved in the token
stream. For instance, the text `Hello, World!` would consist of three tokens: a
text token (`Hello,`), a whitespace token (` `, and lastly followed by a text
token (`World!`).

Certain tokens are classified and tokenized differently depending on their
preceding context. Consider for instance `a sentence (with a parenthesis)` which
is simply text tokens separated by whitespace tokens, as opposed to `a
[sentence](with-a-link)` where instead need to identify both the link
(`sentence`) and it's corresponding URL (`with-a-link`). Other similar examples
are headings, which are denoted by a series of pound runes (`#`) at the start of
a line, or heading anchors `{#like-so}`, which may only appear on a heading
line.

### Recognition {#recognition}

Once a Markdown document has been tokenized, the stream of token is then pattern
matched and recognized into a stream of patterns. As an example, depending on
placement, the text `[Example]` could be a link's text, a link's cross
reference, both a link's test and its cross reference, or the start of a cross
reference definition.

Implementation wise, the recognition work is done in the `recognizer` which
bridges the [`LintRuleOverTokens` rule](#rules) to a [`LintRuleOverPatterns`
rule](#rule).

### Rules {#rules}

There are two sets of rules supported, rules over tokens, and rules over
patterns. Both of these have common behavior which we describe first.

**Common behavior**

All rules are invoked:

* On start, i.e. when the linter starts.
* On document start, i.e. when the linter starts to parse a new document.
* On document end, i.e. when the linter completes the parsing of a new document.
* On end, i.e. when the linter completes.

**Over tokens**

Rules over tokens are additionally invoked after a document starts to parse, and
before a document completes:

* On each token, i.e. as the name suggests.

**Over patterns**

Rules over patterns are additionally invoked after a document starts to parse,
and before a document completes, for every pattern encountered. A non-exhaustive
list includes:

* When a link using a cross reference is used.
* When a link using a URL is used.
* On the definition of a cross reference.

### Defining a new rule

Each rule should be defined in its own file named `example_rule.go`.
Rules should include a description, which by convention is placed in
the test file. The convention is to follow the pattern:

```go
package rules

import (
	"go.fuchsia.dev/fuchsia/tools/mdlint/core"
)

func init() {
	// or core.RegisterLintRuleOverPatterns(...)
	core.RegisterLintRuleOverTokens(exampleRuleName, newExampleRule)
}

const exampleRuleName = "example-rule"

type exampleRule struct {
    ...
}

var _ core.LintRuleOverTokens = (*exampleRule)(nil) // or core.LintRuleOverPatterns

func newExampleRule(reporter core.Reporter) core.LintRuleOverTokens {
    return &exampleRule{ ... }
}

// followed by the implementation
```

### Testing a rule

Rules should be tested using sample Markdown documents, with the help of the
provided testing utilities:

```go
// Description of the rule, with details of the checks provided.

func TestExampleRule_firstCase(t *testing.T) {
	ruleTestCase{
		files: map[string]string{
			"first.md": `Sample Markdown document

Use a «marker» to denote expected warnings.

You can place markers on whitespace, for instance« »
denotes an expected warning on a non-trimmed line.`,

			"second.md": `Another Markdown document here.`,
		},
	// or runOverPatterns
	}.runOverTokens(t, newExampleRule)
}
```

In multi-files tests, we rely non the non-deterministic iteration order of maps
to ensure that rules do not rely on a specific file order for their correctness.
Consider running new tests multiple times using the `go test` flag `count` to
verify the robustness of your rule.
