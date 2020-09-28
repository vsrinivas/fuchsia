// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::input::consumer_control::Phase;
use fidl_fuchsia_input_report::ConsumerControlButton;

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum FactoryResetState {
    Waiting,
    StartCountdown,
    CancelCountdown,
    ExecuteReset,
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum ResetEvent {
    ButtonPress(ConsumerControlButton, Phase),
    CountdownFinished,
    CountdownCancelled,
}

pub struct FactoryResetStateMachine {
    volume_up_phase: Phase,
    volume_down_phase: Phase,
    state: FactoryResetState,
}

impl FactoryResetStateMachine {
    pub fn new() -> FactoryResetStateMachine {
        FactoryResetStateMachine {
            volume_down_phase: Phase::Up,
            volume_up_phase: Phase::Up,
            state: FactoryResetState::Waiting,
        }
    }

    pub fn is_counting_down(&self) -> bool {
        self.state == FactoryResetState::StartCountdown
    }

    fn update_button_state(&mut self, button: ConsumerControlButton, phase: Phase) {
        match button {
            ConsumerControlButton::VolumeUp => self.volume_up_phase = phase,
            ConsumerControlButton::VolumeDown => self.volume_down_phase = phase,
            _ => panic!("Invalid button provided {:?}", button),
        };
    }

    fn check_buttons_pressed(&self) -> bool {
        match (self.volume_up_phase, self.volume_down_phase) {
            (Phase::Down, Phase::Down) => true,
            _ => false,
        }
    }

    pub fn handle_event(&mut self, event: ResetEvent) -> FactoryResetState {
        let new_state: FactoryResetState = match self.state {
            FactoryResetState::Waiting => {
                match event {
                    ResetEvent::ButtonPress(button, phase) => {
                        self.update_button_state(button, phase);
                        if self.check_buttons_pressed() {
                            println!("recovery: start reset countdown");
                            FactoryResetState::StartCountdown
                        } else {
                            FactoryResetState::Waiting
                        }
                    },
                    ResetEvent::CountdownCancelled | ResetEvent::CountdownFinished =>
                        panic!("Not expecting timer updates when in waiting state"),
                }
            },
            FactoryResetState::StartCountdown => {
                match event {
                    ResetEvent::ButtonPress(button, phase) => {
                        self.update_button_state(button, phase);
                        if self.check_buttons_pressed() {
                            panic!("Not expecting both buttons to be pressed while in StartCountdown state");
                        } else {
                            println!("recovery: cancel reset countdown");
                            FactoryResetState::CancelCountdown
                        }
                    },
                    ResetEvent::CountdownCancelled => panic!("Not expecting CountdownCancelled here, expecting input event to move to CancelCountdown state first."),
                    ResetEvent::CountdownFinished => {
                        println!("recovery: execute factory reset");
                        FactoryResetState::ExecuteReset
                    }
                }
            },
            FactoryResetState::CancelCountdown => {
                match event {
                    ResetEvent::CountdownCancelled => FactoryResetState::Waiting,
                    _ => panic!("Only expecting CountdownCancelled event in CancelCountdown state"),
                }
            },
            FactoryResetState::ExecuteReset => {
                match event {
                    ResetEvent::ButtonPress(_, _) => {
                        println!("Ignoring button press while executing factory reset");
                        FactoryResetState::ExecuteReset
                    },
                    ResetEvent::CountdownCancelled | ResetEvent::CountdownFinished => {
                        panic!("Not expecting countdown events while in ExecuteReset state")
                    },
                }
            },
        };

        self.state = new_state;
        return new_state;
    }

    #[cfg(test)]
    pub fn get_state(&self) -> FactoryResetState {
        return self.state;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_reset_complete() -> std::result::Result<(), anyhow::Error> {
        let mut state_machine = FactoryResetStateMachine::new();
        let state = state_machine.get_state();
        assert_eq!(state, FactoryResetState::Waiting);
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeUp, Phase::Down));
        assert_eq!(state, FactoryResetState::Waiting);
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeDown, Phase::Down));
        assert_eq!(state, FactoryResetState::StartCountdown);
        let state = state_machine.handle_event(ResetEvent::CountdownFinished);
        assert_eq!(state, FactoryResetState::ExecuteReset);
        Ok(())
    }

    #[test]
    fn test_reset_complete_reverse() -> std::result::Result<(), anyhow::Error> {
        let mut state_machine = FactoryResetStateMachine::new();
        let state = state_machine.get_state();
        assert_eq!(state, FactoryResetState::Waiting);
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeDown, Phase::Down));
        assert_eq!(state, FactoryResetState::Waiting);
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeUp, Phase::Down));
        assert_eq!(state, FactoryResetState::StartCountdown);
        let state = state_machine.handle_event(ResetEvent::CountdownFinished);
        assert_eq!(state, FactoryResetState::ExecuteReset);
        Ok(())
    }

    #[test]
    fn test_reset_cancelled() -> std::result::Result<(), anyhow::Error> {
        test_reset_cancelled_button(ConsumerControlButton::VolumeUp);
        test_reset_cancelled_button(ConsumerControlButton::VolumeDown);
        Ok(())
    }

    fn test_reset_cancelled_button(button: ConsumerControlButton) {
        let mut state_machine = FactoryResetStateMachine::new();
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeUp, Phase::Down));
        assert_eq!(state, FactoryResetState::Waiting);
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeDown, Phase::Down));
        assert_eq!(state, FactoryResetState::StartCountdown);
        let state = state_machine.handle_event(ResetEvent::ButtonPress(button, Phase::Up));
        assert_eq!(state, FactoryResetState::CancelCountdown);
        let state = state_machine.handle_event(ResetEvent::CountdownCancelled);
        assert_eq!(state, FactoryResetState::Waiting);
        let state = state_machine.handle_event(ResetEvent::ButtonPress(button, Phase::Down));
        assert_eq!(state, FactoryResetState::StartCountdown);
    }

    #[test]
    #[should_panic]
    fn test_cancel_early() {
        let mut state_machine = FactoryResetStateMachine::new();
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeUp, Phase::Down));
        assert_eq!(state, FactoryResetState::Waiting);
        let _state = state_machine.handle_event(ResetEvent::CountdownCancelled);
    }

    #[test]
    #[should_panic]
    fn test_early_complete_countdown() {
        let mut state_machine = FactoryResetStateMachine::new();
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeUp, Phase::Down));
        assert_eq!(state, FactoryResetState::Waiting);
        let _state = state_machine.handle_event(ResetEvent::CountdownFinished);
    }

    #[test]
    #[should_panic]
    fn test_cancelled_countdown_not_complete() {
        let mut state_machine = FactoryResetStateMachine::new();
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeUp, Phase::Down));
        assert_eq!(state, FactoryResetState::Waiting);
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeDown, Phase::Down));
        assert_eq!(state, FactoryResetState::StartCountdown);
        let state = state_machine
            .handle_event(ResetEvent::ButtonPress(ConsumerControlButton::VolumeDown, Phase::Up));
        assert_eq!(state, FactoryResetState::CancelCountdown);
        let _state = state_machine.handle_event(ResetEvent::CountdownFinished);
    }
}
