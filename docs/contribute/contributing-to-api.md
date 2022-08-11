# Contributing to API

To contribute to the [Fuchsia API Surface][fuchsia-api-surface], do the following:

*  Evaluate whether your change is large or small.

   *  If you have a small, incremental change to the API, contribute your
      change by completing the steps in
      [Create a change in Gerrit][create-a-change-in-gerrit], as you would for
      any Fuchsia source code change.
   *  If you have a large change to the API, that is, a change that
      significantly expands on the function of the API or modifies the
      API extensively, do the following:

      *  Create an [RFC][rfc] that explains the design of your modification
         to the API.
      *  This RFC should be reviewed through the normal [RFC process][rfc-process].
         The API reviewer for the relevant area should be a stakeholder in the RFC. See
         the [Fuchsia API Council Charter][api-council] to identify API reviewers.
      *  After your API RFC is approved, contribute your change by completing the steps
         in [Create a change in Gerrit][create-a-change-in-gerrit], as you would
         for any Fuchsia source code change.

* [Request a code review][request-a-code-review] from an API council member. Select
  your API council reviewer based on the area of the Fuchsia API that you're modifying.
  For a list of API council members and their areas of focus, see [Membership][membership]
  in the Fuchsia API Council Charter.

<!-- Reference links -->

[fuchsia-api-surface]: /docs/glossary/README.md#fuchsia-api-surface
[create-a-change-in-gerrit]: /docs/development/source_code/contribute_changes.md#create-a-change-in-gerrit
[request-a-code-review]: /docs/development/source_code/contribute_changes.md#request-a-code-review
[rfc]: /docs/contribute/governance/rfcs/TEMPLATE.md
[rfc-process]: /docs/contribute/governance/rfcs/rfc_process.md
[api-council]: /docs/contribute/governance/api_council.md#area
[membership]: /docs/contribute/governance/api_council.md#membership
