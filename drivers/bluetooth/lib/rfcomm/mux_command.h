#pragma once

#include <cstddef>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "rfcomm.h"

namespace btlib {
namespace rfcomm {

// Defined in GSM 5.4.6.3.*.
// clang-format off
enum class MuxCommandType : uint8_t {
  kDLCParameterNegotiation      = 0b10000000,
  kTestCommand                  = 0b00100000,
  kFlowControlOnCommand         = 0b10100000,
  kFlowControlOffCommand        = 0b01100000,
  kModemStatusCommand           = 0b11100000,
  kNonSupportedCommandResponse  = 0b00010000,
  kRemotePortNegotiationCommand = 0b10010000,
  kRemoteLineStatusCommand      = 0b01010000
};
// clang-format on

// A MuxCommand represents an RFCOMM multiplexer command which is ready to be
// sent or which has been parsed from a ByteBuffer. Specifically, each
// MuxCommand can be seen as a collection of fields and their accessors, plus a
// function Write which knows how to write these fields into a buffer
// in the format defined by the RFCOMM/GSM specs.
//
// Supported commands are listed in RFCOMM 4.3.
class MuxCommand {
 public:
  // Parses a buffer and constructs the appropriate subclass of MuxCommand.
  // Users of Parse should read the resulting command type and cast the pointer
  // to the appropriate MuxCommand subclass.
  static std::unique_ptr<MuxCommand> Parse(const common::ByteBuffer& buffer);

  virtual ~MuxCommand() = default;

  // Writes this MuxCommand into |buffer|. |buffer| must be at least the size
  // indicated by written_size().
  virtual void Write(common::MutableBufferView buffer) const = 0;

  // The amount of space this command takes up when written. This is used when
  // allocating space for an RFCOMM frame which will hold a multiplexer command.
  virtual size_t written_size() const = 0;

  inline MuxCommandType command_type() const { return command_type_; }

  // The multiplexer-command-level C/R setting. This setting is described in GSM
  // 5.4.6.2.
  inline CommandResponse command_response() const { return command_response_; }

 protected:
  inline MuxCommand(MuxCommandType command_type,
                    CommandResponse command_response)
      : command_type_(command_type), command_response_(command_response){};

  // Forms the type field octet for this multiplexer command by ORing:
  //  - the EA bit (which is always 1 for GSM/RFCOMM -- see GSM 5.4.6.1)
  //  - the C/R bit (whose value is defined in 5.4.6.2), and
  //  - the type octet for the command in question (see 5.4.6.3.*).
  inline uint8_t type_field_octet() const {
    return 1 | ((command_response_ == CommandResponse::kCommand ? 1 : 0) << 1) |
           (uint8_t)command_type_;
  }

  MuxCommandType command_type_;
  CommandResponse command_response_;
};

// This command is used to test the connection between two peers. The command
// contains an attached test pattern, which the peer must repeat back in full.
// See GSM 5.4.6.3.4.
//
// Multiplexer frames can encode an arbitrary number of length octets. In our
// implementation, the length of the pattern contained within the test command
// is limited by the system's max value of size_t.
class TestCommand : public MuxCommand {
 public:
  TestCommand(CommandResponse command_response,
              const common::ByteBuffer& test_pattern);

  // Returns nullptr if parse fails. |command_response| and |length| are
  // parameters which have already been parsed from |buffer|.
  static std::unique_ptr<TestCommand> Parse(CommandResponse command_response,
                                            size_t length,
                                            const common::ByteBuffer& buffer);

  inline common::BufferView test_pattern() const {
    return test_pattern_.view();
  }

  // MuxCommand overrides
  void Write(common::MutableBufferView buffer) const override;
  size_t written_size() const override;

 private:
  common::DynamicByteBuffer test_pattern_;
};

// This command is sent when our device is able to begin receiving messages. See
// GSM 5.4.6.3.5.
class FlowControlOnCommand : public MuxCommand {
 public:
  inline FlowControlOnCommand(CommandResponse command_response)
      : MuxCommand(MuxCommandType::kFlowControlOnCommand, command_response) {}

  // Returns nullptr if parse fails. |command_response| is a parameter which has
  // already been parsed from |buffer|.
  static std::unique_ptr<FlowControlOnCommand> Parse(
      CommandResponse command_response);

  // MuxCommand overrides
  void Write(common::MutableBufferView buffer) const override;
  size_t written_size() const override;
};

// This command is sent when our device is not able to receive messages. See
// GSM 5.4.6.3.6.
class FlowControlOffCommand : public MuxCommand {
 public:
  inline FlowControlOffCommand(CommandResponse command_response)
      : MuxCommand(MuxCommandType::kFlowControlOffCommand, command_response) {}

  // Returns nullptr if parse fails. |command_response| is a parameter which has
  // already been parsed from |buffer|.
  static std::unique_ptr<FlowControlOffCommand> Parse(
      CommandResponse command_response);

  // MuxCommand overrides
  void Write(common::MutableBufferView buffer) const override;
  size_t written_size() const override;
};

// These signals map to various V.24 signals as described in GSM tables 6 and 7.
struct ModemStatusCommandSignals {
  bool flow_control;
  bool ready_to_communicate;
  bool ready_to_receive;
  bool incoming_call;
  bool data_valid;
};

using BreakValue = uint8_t;
constexpr BreakValue kMinBreakValue = 0b0000;
constexpr BreakValue kMaxBreakValue = 0b1111;
constexpr BreakValue kDefaultInvalidBreakValue = 0xFF;

// This command conveys the virtual V.24 signals. See GSM 5.4.6.3.7.
class ModemStatusCommand : public MuxCommand {
 public:
  inline ModemStatusCommand(CommandResponse command_response, DLCI dlci,
                            ModemStatusCommandSignals signals,
                            BreakValue break_value = kDefaultInvalidBreakValue)
      : MuxCommand(MuxCommandType::kModemStatusCommand, command_response),
        dlci_(dlci),
        signals_(signals),
        break_value_(break_value){};

  // Returns nullptr if parse fails. |command_response| and |length| are
  // parameters which have already been parsed from |buffer|.
  static std::unique_ptr<ModemStatusCommand> Parse(
      CommandResponse command_response, size_t length,
      const common::ByteBuffer& buffer);

  // MuxCommand overrides
  void Write(common::MutableBufferView buffer) const override;

  size_t written_size() const override;

  inline DLCI dlci() const { return dlci_; }

  inline ModemStatusCommandSignals signals() const { return signals_; }

  // We only include a break signal if the break value is valid.
  bool has_break_signal() const {
    return break_value_ >= kMinBreakValue && break_value_ <= kMaxBreakValue;
  }

  inline BreakValue break_value() const { return break_value_; }

 private:
  DLCI dlci_;
  ModemStatusCommandSignals signals_;
  BreakValue break_value_;
};

// RFCOMM table 5.3.
enum class CreditBasedFlowHandshake : uint8_t {
  kUnsupported = 0x0,
  kSupportedRequest = 0xF,
  kSupportedResponse = 0xE
};

using Priority = uint8_t;
constexpr Priority kMinPriority = 0;
constexpr Priority kMaxPriority = 63;

using InitialCredits = uint8_t;
constexpr InitialCredits kMinInitialCredits = 0;
constexpr InitialCredits kMaxInitialCredits = 7;

// See GSM 5.4.6.3.1 and the modifications presented in RFCOMM 5.5.3.
struct ParameterNegotiationParams {
  // 6 bits wide.
  DLCI dlci;
  CreditBasedFlowHandshake credit_based_flow_handshake;
  Priority priority;
  uint16_t maximum_frame_size;
  InitialCredits initial_credits;
};

// This command is used prior to opening a DLC. It is used to set up the
// parameters of the DLC. See GSM 5.4.6.3.1 and the modifications described in
// RFCOMM 5.5.3.
class DLCParameterNegotiationCommand : public MuxCommand {
 public:
  inline DLCParameterNegotiationCommand(CommandResponse command_response,
                                        ParameterNegotiationParams params)
      : MuxCommand(MuxCommandType::kDLCParameterNegotiation, command_response),
        params_(params) {}

  // Returns nullptr if parse fails. |command_response| is a parameter which has
  // already been parsed from |buffer|.
  static std::unique_ptr<DLCParameterNegotiationCommand> Parse(
      CommandResponse command_response, const common::ByteBuffer& buffer);

  // MuxCommand overrides
  void Write(common::MutableBufferView buffer) const override;
  size_t written_size() const override;

  inline ParameterNegotiationParams params() const { return params_; }

 private:
  ParameterNegotiationParams params_;
};

// This response is sent when we receive a command which we do not recognize or
// support. See GSM 5.4.6.3.8.
class NonSupportedCommandResponse : public MuxCommand {
 public:
  // Note that NSC is always a response.
  // |incoming_non_supported_command| is 6 bits.
  inline NonSupportedCommandResponse(CommandResponse incoming_command_response,
                                     uint8_t incoming_non_supported_command)
      : MuxCommand(MuxCommandType::kNonSupportedCommandResponse,
                   CommandResponse::kResponse),
        incoming_command_response_(incoming_command_response),
        incoming_non_supported_command_(incoming_non_supported_command) {}

  // Returns nullptr if parse fails. |command_response| is a parameter which has
  // already been parsed from |buffer|.
  static std::unique_ptr<NonSupportedCommandResponse> Parse(
      CommandResponse command_response, const common::ByteBuffer& buffer);

  // MuxCommand overrides
  void Write(common::MutableBufferView buffer) const override;
  size_t written_size() const override;

  inline CommandResponse incoming_command_response() const {
    return incoming_command_response_;
  }
  inline uint8_t incoming_non_supported_command() const {
    return incoming_non_supported_command_;
  }

 private:
  CommandResponse incoming_command_response_;
  uint8_t incoming_non_supported_command_;
};

}  // namespace rfcomm
}  // namespace btlib
