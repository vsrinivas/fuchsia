use fidl_fuchsia_ui_input as uii;

pub fn default_state() -> uii::TextInputState {
    uii::TextInputState {
        revision: 1,
        text: "".to_string(),
        selection: uii::TextSelection {
            base: -1,
            extent: -1,
            affinity: uii::TextAffinity::Upstream,
        },
        composing: uii::TextRange { start: -1, end: -1 },
    }
}

pub fn clone_state(state: &uii::TextInputState) -> uii::TextInputState {
    uii::TextInputState {
        revision: state.revision,
        text: state.text.clone(),
        selection: uii::TextSelection {
            base: state.selection.base,
            extent: state.selection.extent,
            affinity: state.selection.affinity,
        },
        composing: uii::TextRange {
            start: state.composing.start,
            end: state.composing.end,
        },
    }
}
