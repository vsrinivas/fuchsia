// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_diagnostics::Selector, lazy_static::lazy_static};

lazy_static! {
    #[derive(Clone)]
    static ref MONIKERS_TO_REWRITE: Vec<MonikerRewritePair> = vec![MonikerRewritePair {
        // Note: These are unparsed strings, not separated into moniker segments.
        // legacy_str cannot contain slashes but modern_str can.
        legacy_str: "memory_monitor.cmx",
        modern_str: "core/memory_monitor",
    }];
}

pub struct MonikerRewriter {
    monikers: Vec<MonikerRewritePair>,
}

#[derive(Clone, Debug)]
pub struct MonikerRewritePair {
    legacy_str: &'static str,
    modern_str: &'static str,
}

impl MonikerRewriter {
    pub(crate) fn new() -> Self {
        Self { monikers: MONIKERS_TO_REWRITE.to_vec() }
    }

    /// Checks whether any of the selectors needs rewriting. If so, it returns an OutputRewriter
    /// that makes the corresponding change on the output. Otherwise, it returns None for the
    /// rewriter. Either way, it returns the correct set of selectors.
    pub(crate) fn rewrite_selectors(
        &self,
        mut selectors: Vec<Selector>,
    ) -> (Option<Vec<Selector>>, Option<OutputRewriter>) {
        let mut monikers_rewritten = Vec::new();
        for s in selectors.iter_mut() {
            for pair in &self.monikers {
                // Not expecting an error here, but if we get one, don't make any changes.
                if selectors::match_component_moniker_against_selector(&[pair.legacy_str], &s)
                    .unwrap_or(false)
                {
                    if let Ok(selector) = selectors::parse_component_selector(pair.modern_str) {
                        s.component_selector = Some(selector);
                        monikers_rewritten.push(pair.clone());
                    }
                }
            }
        }
        let rewriter = if monikers_rewritten.len() > 0 {
            Some(OutputRewriter { monikers: monikers_rewritten })
        } else {
            None
        };
        (Some(selectors), rewriter)
    }
}

#[derive(Debug)]
pub struct OutputRewriter {
    monikers: Vec<MonikerRewritePair>,
}

impl OutputRewriter {
    pub(crate) fn rewrite_moniker(&self, moniker: String) -> String {
        for pair in &self.monikers {
            if pair.modern_str == moniker {
                return pair.legacy_str.to_string();
            }
        }
        return moniker;
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_diagnostics::StringSelector;

    macro_rules! extract_moniker {
        ($selector:expr) => {
            extract_moniker!($selector, 0)
        };
        ($selectors:expr, $index:expr) => {
            $selectors.as_ref().unwrap()[$index]
                .component_selector
                .as_ref()
                .unwrap()
                .moniker_segments
                .as_ref()
                .unwrap()
        };
    }

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
            extract_moniker!(rewritten_legacy),
            &vec![
                StringSelector::StringPattern("core".to_string()),
                StringSelector::StringPattern("memory_monitor".to_string())
            ]
        );
        assert_eq!(
            extract_moniker!(rewritten_new),
            &vec![
                StringSelector::StringPattern("core".to_string()),
                StringSelector::StringPattern("memory_monitor".to_string())
            ]
        );
        assert_eq!(
            extract_moniker!(rewritten_irrelevant),
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

    #[test]
    fn array_logic_works() {
        let rewriter = MonikerRewriter {
            monikers: vec![
                MonikerRewritePair { legacy_str: "legacy1", modern_str: "modern1" },
                MonikerRewritePair { legacy_str: "legacy2", modern_str: "modern2" },
            ],
        };
        let legacy_selector1 = selectors::parse_selector("legacy1:path/to:data").unwrap();
        let legacy_selector2 = selectors::parse_selector("legacy2:path/to:data").unwrap();
        let irrelevant_selector = selectors::parse_selector("irrelevant:path/to:data").unwrap();

        let (rewritten_1_i, rewriter_1_i) =
            rewriter.rewrite_selectors(vec![legacy_selector1.clone(), irrelevant_selector.clone()]);
        let (rewritten_2_1, rewriter_2_1) =
            rewriter.rewrite_selectors(vec![legacy_selector2.clone(), legacy_selector1.clone()]);
        let rewriter_1_i = rewriter_1_i.unwrap();
        let rewriter_2_1 = rewriter_2_1.unwrap();

        assert_eq!(
            extract_moniker!(rewritten_1_i, 0),
            &vec![StringSelector::StringPattern("modern1".to_string()),]
        );
        assert_eq!(
            extract_moniker!(rewritten_1_i, 1),
            &vec![StringSelector::StringPattern("irrelevant".to_string()),]
        );
        assert_eq!(
            extract_moniker!(rewritten_2_1, 0),
            &vec![StringSelector::StringPattern("modern2".to_string()),]
        );
        assert_eq!(
            extract_moniker!(rewritten_2_1, 1),
            &vec![StringSelector::StringPattern("modern1".to_string()),]
        );

        assert_eq!(rewriter_1_i.rewrite_moniker("modern1".to_string()), "legacy1".to_string());
        assert_eq!(rewriter_1_i.rewrite_moniker("modern2".to_string()), "modern2".to_string());
        assert_eq!(rewriter_2_1.rewrite_moniker("modern1".to_string()), "legacy1".to_string());
        assert_eq!(rewriter_2_1.rewrite_moniker("modern2".to_string()), "legacy2".to_string());
    }
}
