// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/80913): Update this code to a more general solution.
// This code currently only supports fxbug.dev/81282.

use fidl_fuchsia_diagnostics::Selector;

// Note: These are unparsed strings, not separated into moniker segments.
// TODO(fxbug.dev/80913): legacy / v1 monikers can contain slashes and would need to be matched
// piecewise.
const LEGACY_MEMORY_MONIKER_STR: &str = "memory_monitor.cmx";
const MODERN_MEMORY_MONIKER_STR: &str = "core/memory_monitor";

pub struct MonikerRewriter {
    legacy_str: &'static str,
    modern_str: &'static str,
}

impl MonikerRewriter {
    pub(crate) fn new() -> Self {
        Self { legacy_str: LEGACY_MEMORY_MONIKER_STR, modern_str: MODERN_MEMORY_MONIKER_STR }
    }

    // TODO(fxbug.dev/80913): Add a chained-initializer function to add (legacy, modern) pairs
    // to the struct. (Also, elsewhere, infrastructure to load those pairs from a config file.)

    /// Checks whether any of the selectors needs rewriting. If so, it returns an OutputRewriter
    /// that makes the corresponding change on the output. Otherwise, it returns None for the
    /// rewriter. Either way, it returns the correct set of selectors.
    pub(crate) fn rewrite_selectors(
        &self,
        mut selectors: Vec<Selector>,
    ) -> (Option<Vec<Selector>>, Option<OutputRewriter>) {
        let mut rewrote = false;
        for s in selectors.iter_mut() {
            // Not expecting an error here, but if we get one, don't make any changes.
            if selectors::match_component_moniker_against_selector(&[self.legacy_str], &s)
                .unwrap_or(false)
            {
                if let Ok(selector) = selectors::parse_component_selector(self.modern_str) {
                    s.component_selector = Some(selector);
                    rewrote = true;
                }
            }
        }
        let rewriter = if rewrote {
            Some(OutputRewriter { legacy: self.legacy_str, modern_str: self.modern_str })
        } else {
            None
        };
        (Some(selectors), rewriter)
    }
}

#[derive(Debug)]
pub struct OutputRewriter {
    legacy: &'static str,
    modern_str: &'static str,
}

impl OutputRewriter {
    pub(crate) fn rewrite_moniker(&self, moniker: String) -> String {
        if self.modern_str == moniker {
            self.legacy.to_string()
        } else {
            moniker
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_diagnostics::StringSelector;

    #[test]
    fn moniker_rewriter_works() {
        let rewriter = MonikerRewriter::new();
        let legacy_selector = selectors::parse_selector("memory_monitor.cmx:path/to:data").unwrap();
        let new_selector = selectors::parse_selector("core/memory_monitor:path/to:data").unwrap();
        let irrelevant_selector = selectors::parse_selector("foo/bar.baz:path/to:data").unwrap();

        let (rewritten_legacy, legacy_rewriter) = rewriter.rewrite_selectors(vec![legacy_selector]);
        let (rewritten_new, new_rewriter) = rewriter.rewrite_selectors(vec![new_selector]);
        let (rewritten_irrelevant, irrelevant_rewriter) =
            rewriter.rewrite_selectors(vec![irrelevant_selector]);

        assert_eq!(
            rewritten_legacy.unwrap()[0]
                .component_selector
                .as_ref()
                .unwrap()
                .moniker_segments
                .as_ref()
                .unwrap(),
            &vec![
                StringSelector::StringPattern("core".to_string()),
                StringSelector::StringPattern("memory_monitor".to_string())
            ]
        );
        assert_eq!(
            rewritten_new.unwrap()[0]
                .component_selector
                .as_ref()
                .unwrap()
                .moniker_segments
                .as_ref()
                .unwrap(),
            &vec![
                StringSelector::StringPattern("core".to_string()),
                StringSelector::StringPattern("memory_monitor".to_string())
            ]
        );
        assert_eq!(
            rewritten_irrelevant.unwrap()[0]
                .component_selector
                .as_ref()
                .unwrap()
                .moniker_segments
                .as_ref()
                .unwrap(),
            &vec![
                StringSelector::StringPattern("foo".to_string()),
                StringSelector::StringPattern("bar.baz".to_string())
            ]
        );
        assert!(new_rewriter.is_none());
        assert!(irrelevant_rewriter.is_none());
        let rewriter = legacy_rewriter.unwrap();

        assert_eq!(rewriter.rewrite_moniker("foo/bar.baz".to_string()), "foo/bar.baz".to_string());
        assert_eq!(
            rewriter.rewrite_moniker("memory_monitor.cmx".to_string()),
            "memory_monitor.cmx".to_string()
        );
        assert_eq!(
            rewriter.rewrite_moniker("core/memory_monitor".to_string()),
            "memory_monitor.cmx".to_string()
        );
    }
}
