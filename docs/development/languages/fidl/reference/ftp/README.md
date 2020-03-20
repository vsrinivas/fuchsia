# FIDL language tuning proposals

Note: Documents are sorted by date reviewed.

## Accepted proposals

FTP                   | Submitted  | Reviewed   | Title
----------------------|------------|------------|----------------------------------
[FTP-001](ftp-001.md) | 2018-07-17 | 2018-08-01 | FTP process
[FTP-002](ftp-002.md) | 2018-07-17 | 2018-08-03 | Type aliases with "using" keyword
[FTP-009](ftp-009.md) | 2018-07-31 | 2018-08-20 | Documentation comments
[FTP-012](ftp-012.md) | 2018-08-30 | 2018-09-11 | Empty structs
[FTP-007](ftp-007.md) | 2018-07-27 | 2018-09-20 | Tables
[FTP-003](ftp-003.md) | 2018-07-17 | 2018-09-27 | Clarification: Default Values for Struct Members
[FTP-008](ftp-008.md) | 2018-07-19 | 2018-10-04 | Epitaphs
[FTP-013](ftp-013.md) | 2018-09-07 | 2018-10-11 | Introduce a `[Deprecated]` Attribute
[FTP-015](ftp-015.md) | 2018-09-20 | 2018-10-11 | Extensible Unions
[FTP-021](ftp-021.md) | 2018-10-31 | 2018-11-01 | Soft Transitions for Methods Add / Remove
[FTP-020](ftp-020.md) | 2018-10-26 | 2018-11-29 | Interface Ordinal Hashing
[FTP-014](ftp-014.md) | 2018-09-18 | 2018-12-06 | Error Handling
[FTP-023](ftp-023.md) | 2018-12-10 | 2019-01-09 | Compositional Model for Protocols
[FTP-006](ftp-006.md) | 2018-07-20 | 2019-01-14 | Programmer Advisory Explicit Defaults
[FTP-025](ftp-025.md) | 2019-01-09 | 2019-01-24 | Bit Flags &mdash; Just a Little Bit
[FTP-030](ftp-030.md) | 2019-01-30 | 2019-01-30 | FIDL is little endian
[FTP-027](ftp-027.md) | 2019-01-19 | 2019-02-04 | You only pay for what you use
[FTP-032](ftp-032.md) | 2019-02-06 | 2019-02-21 | Efficient Envelopes: Turning Envelopes into Postcards
[FTP-029](ftp-029.md) | 2019-02-14 | 2019-02-28 | Increasing Method Ordinals
[FTP-033](ftp-033.md) | 2019-02-07 | 2019-03-07 | Handling of Unknown Fields &amp; Strictness
[FTP-004](ftp-004.md) | 2018-07-19 | 2019-03-14 | Safer Structs for C++
[FTP-037](ftp-037.md) | 2019-03-07 | 2019-03-14 | Transactional Message Header v3
[FTP-024](ftp-024.md) | 2019-04-02 | 2019-04-11 | Mandatory Source Compatibility
[FTP-041](ftp-041.md) | 2019-04-08 | 2019-04-23 | Support for Unifying Services and Devices
[FTP-043](ftp-043.md) | 2019-05-06 | 2019-05-30 | Documentation Comment Format &mdash; Mark me up, mark me down
[FTP-048](ftp-048.md) | 2019-08-25 | 2019-09-26 | Explicit Union Ordinals
[FTP-054](ftp-054.md) | 2019-11-21 | 2019-12-12 | Parameter Attributes &mdash; A Chance to Write Self Documenting APIs
[FTP-049](ftp-049.md) | 2019-11-20 | 2019-12-19 | FIDL Tuning Process Evolution

## Rejected proposals

FTP                   | Submitted  | Reviewed   | Title
----------------------|------------|------------|----------------------------------
[FTP-005](ftp-005.md) | 2018-07-19 | 2018-09-11 | Method Impossible
[FTP-010](ftp-010.md) | 2018-07-31 | 2018-10-04 | `[OrdinalRange]`, where the deer and the antelope roam
[FTP-016](ftp-016.md) | 2018-09-27 | 2018-10-25 | No Optional Strings or Vectors
[FTP-026](ftp-026.md) | 2019-01-19 | 2019-02-04 | Envelopes Everywhere
[FTP-035](ftp-035.md) | 2019-02-28 | *withdrawn*| Automatic Flow Tracing
[FTP-036](ftp-036.md) | 2019-03-07 | 2019-03-14 | Update to Struct Declarations
[FTP-042](ftp-042.md) | 2019-04-01 | 2019-04-01 | Non Nullable Types &mdash; Poisson d'Avril
[FTP-040](ftp-040.md) | 2019-04-07 | 2019-04-18 | Identifier Uniqueness &mdash; SnowFlake vs SNOW_FLAKE
[FTP-045](ftp-045.md) | 2018-12-26 | 2019-05-29 | Zero-Size Empty Structs: &infin;% more efficient

## Process

The FIDL Tuning Proposal (FTP) process is designed to provide a
uniform and recorded path for making changes to the [FIDL] language,
bindings, and tools.

* See [FTP-001: FTP process](ftp-001.md)
* See [FTP-049: FIDL Tuning Process Evolution](ftp-049.md)

### Criteria for requiring an FTP

A change MUST go through the FTP process when either:

1. The **solution space is large**, i.e. the change is one of many possibly good
   other solutions and there is a difficult design tradeoff to make;

2. The **change has a large impact**, i.e. The change modifies the behavior of FIDL
   in a substantial way such that it may introduce risk to many-or-all users of
   FIDL;

3. The **change has a large scope**, i.e. The change touches enough pieces of FIDL
   such that careful attention is required to determine whether it may or may
   not have a large impact.

For instance, changes to the following areas will likely require an FTP:

* FIDL governance
* Design principles
* Language grammar
* Type system
* Protocol semantics
* Wire format
* Bindings specification

Additional details are provided in
[FTP-049: FIDL Tuning Process Evolution](ftp-049.md).

### Design

An FTP (FIDL Tuning Proposal) goes through several stages. These
stages correspond to the `Status:` field of the heading of the template.

NB: The template is currently Google-internal.

#### Draft

One or more people get excited about a change! They make a copy of the
tuning template, and start writing and designing. The proposal should
address each of the section headings in the template, even if it is
only to say "Not Applicable".

At this stage they may start soliciting feedback on the draft from impacted parties.

#### Comment

At this stage, the FTP is formally circulated for commentary to the
Fuchsia engineering organization. The authors of the proposal should
solicit feedback from those especially likely to be impacted by the
proposal.

For now, proposals should be left open for comment for at least one
week, subject to reviewer discretion. It may be reasonable to be
shorter for less controversial FTPs, and longer to wait for feedback
from a particular person or group to come in.

Anyone may make a blocking comment on an FTP. Blocking comments do not
prevent a particular accept-or-reject outcome from the review process,
but reviewers are required to acknowledge the feedback given in the
comment as part of the final FTP.

#### Withdrawing

Withdrawn FTPs are valuable records of engineering ideation. When an author
withdraws their FTP, the withdrawal rationale must be added to the FTP. The FTP
will then be copied to the public record of all FTPs for posterity.

The withdrawal rationale is written by the FTP author, possibly in conjunction
with members of the Fuchsia FIDL team.

The rationale should be actionable in the following two ways.

What did the author learn through the FTP process which would have led them to
propose an alternative design?

What are alternatives to the withdrawn FTP which are promising?

#### Review

At this point the FTP, along with all outstanding commentary, is reviewed.

The proposal is reviewed by members of the Fuchsia FIDL team (defined by an
OWNERS file in the fuchsia.git repository [Location TBD], and unofficially known
as luthiers), and anyone they see fit to include or to delegate to in the
process. For example, they may include a particular language expert when making
a decision about that language's bindings. If necessary, controversial decisions
can be escalated like any other technical decision in Fuchsia.

Most commonly, the review is conducted during one or multiple in-person meetings
‘The FTP review meeting’. The review can also occur using asynchronous
communication if appropriate).

The FTP review meeting starts by the author(s) presenting their design. The
facilitator will then work through the comments in the FTP, asking people who
left comments in the doc to present their feedback.

The facilitator and presenter are ideally different people. The goal of the
facilitator is to ensure that all aspects of the design are addressed, and to
keep the meeting flowing. Ideally, the facilitator does not have a particular
stake in the outcome to avoid the perception of bias, and the presenter
implicitly has a stake in the design they're presenting.

We don't necessarily need to come to closure on every piece of feedback during
the meeting or discuss every last comment (e.g., if there are a large number of
comments or several comments are getting at the same underlying issue). Instead,
the facilitator should optimize for giving the presenter a broad range of
feedback rather than driving each point of debate to a conclusion. Pending open
questions may be resolved in further review sessions, or during Decision making.

#### Decision making

Within five (5) business days, members of the Fuchsia FIDL team (defined by
[`//docs/development/languages/fidl/reference/ftp/OWNERS`](OWNERS) file), with
the ultimate decision maker being the Fuchsia FIDL team lead, decide on the
outcome of the review.

The decision can ultimately have three outcomes.

First, there may be outstanding questions or feedback required to make a
decision. In this case the FTP is moved back to the Comment stage.

Second, the proposal may be Rejected, with reviewers providing a rationale as to
why.

Third, it may be Accepted.

Typically, the venue for decision making will take the form of a meeting. It may
also be an email thread, or happen during a review meeting.

#### Rejected

Rejected FTPs are valuable records of engineering decisions. When
rejected, the rationale for rejected should be added to the FTP. The
FTP will then be copied to the public record of all FTPs for
posterity.

The given rationale should be actionable in the following two senses.

First, what would have to change about the world to have accepted this
proposal?

Second, the rationale should address any blocking comments raised
during the Comment period.

#### Accepted

Accepted FTPs will also have a rationale section appended to them
after review, and will receive a tracking bug.

The same constraints apply to the acceptance rationale as the
rejection rationale. In particular, any blocking comments need to be
addressed.

Then it's off to the races to implement the change.

#### Implemented

At this stage, the proposal is landed. All the code has been
changed. The [tutorial] has been updated. The bug is marked
done. [FIDL] is in a more perfect tuning.

The final step of the process is landing a markdown-ified version of
the FTP into the Fuchsia tree. This applies whether or not the
proposal was accepted, as being able to point at already considered
but rejected proposal is a substantial part of the value of this
process.

[FIDL]: /docs/development/languages/fidl/README.md
[tutorial]: /docs/development/languages/fidl/tutorial/README.md
