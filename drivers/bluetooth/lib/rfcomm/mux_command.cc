#include <cstring>

#include "mux_command.h"

namespace btlib {
namespace rfcomm {

namespace {

// Used to mask different parts of the type and length fields. See GSM 5.4.6.1.
constexpr uint8_t kEAMask = 0b00000001;
constexpr uint8_t kCRMask = 0b00000010;
constexpr uint8_t kTypeMask = 0b11111100;

constexpr size_t kTypeIndex = 0;
constexpr size_t kLengthIndex = 1;

constexpr size_t kLengthShift = 1;

// The information lengths for different types of mux commands. These are the
// only values that should appear in the length field for these commands.
constexpr size_t kPNLength = 8;
constexpr size_t kMSCWithoutBreakLength = 2;
constexpr size_t kMSCWithBreakLength = 3;
constexpr size_t kNSCLength = 1;
constexpr size_t kFConLength = 0;
constexpr size_t kFCoffLength = 0;

constexpr size_t kPNCreditBasedFlowHandshakeShift = 4;

constexpr uint8_t kMSCDLCIShift = 2;
constexpr size_t kMSCFlowControlShift = 1;
constexpr uint8_t kMSCFlowControlMask = 1 << kMSCFlowControlShift;
constexpr size_t kMSCReadyToCommunicateShift = 2;
constexpr uint8_t kMSCReadyToCommunicateMask = 1 << kMSCReadyToCommunicateShift;
constexpr size_t kMSCReadyToReceiveShift = 3;
constexpr uint8_t kMSCReadyToReceiveMask = 1 << kMSCReadyToReceiveShift;
constexpr size_t kMSCIncomingCallShift = 6;
constexpr uint8_t kMSCIncomingCallMask = 1 << kMSCIncomingCallShift;
constexpr size_t kMSCDataValidShift = 7;
constexpr uint8_t kMSCDataValidMask = 1 << kMSCDataValidShift;
constexpr size_t kMSCBreakSignalShift = 1;
constexpr uint8_t kMSCBreakSignalMask = 1 << kMSCBreakSignalShift;
constexpr size_t kMSCBreakValueShift = 4;

constexpr size_t kNSCNotSupportedCommandShift = 2;
constexpr size_t kNSCCRShift = 1;

constexpr uint8_t kPNDLCIMask = 0b00111111;
constexpr uint8_t kPNPriorityMask = 0b00111111;
constexpr uint8_t kPNInitialCreditsMask = 0b00000111;

// Length = 0, EA bit = 1.
constexpr uint8_t kFlowcontrolOnLength = 0b00000001;
constexpr uint8_t kFlowcontrolOffLength = 0b00000001;

// The number of octets which form the header when the length field fits in one
// octet. This is thus the header size for all mux commands with the possible
// exception of the Test command.
constexpr size_t kMinHeaderSize = 2;

// For a given payload |length|, calculates the number of octets needed to
// encode |length|.
// Note that this is only needed by the Test multiplexer command.
size_t NumLengthOctetsNeeded(size_t length) {
  size_t l = length;
  size_t counter = 0;
  while (l) {
    l >>= 1;
    ++counter;
  }
  // Counter now contains the number of least significant bits which are
  // occupied by meaningful data. Subtracting 1, counter becomes the index of
  // the most significant bit containing a 1.

  // If counter is 0, no bits in |length| are set. We still need to return a
  // single octet indicating a value of 0, though.
  size_t num_octets = 1;
  if (counter != 0)
    num_octets = (counter - 1) / 7 + 1;

  return num_octets;
}

// Turns a size_t into a buffer of length field octets as described in GSM
// 5.4.6.1. In this case, we are talking about the length field within the
// multiplexer control commands.
// GSM allows the length field to be specified with a variable number
// of octets.
//
// TODO(gusss): I can't actually find any bounds within the GSM or RFCOMM specs
// on how long this length field is allowed to be. Most of the multiplexer
// commands which we support have fixed-sized payloads (no larger than 8 octets,
// most of the time), so this isn't a problem. However, the Test command takes a
// user-supplied pattern of octets. There is no restriction in the spec on how
// long this pattern can be.
common::DynamicByteBuffer CreateLengthFieldOctets(size_t length) {
  common::DynamicByteBuffer octets(NumLengthOctetsNeeded(length));

  for (size_t i = 0; i < octets.size(); i++) {
    // There should still be meaningful data left in length.
    FXL_DCHECK(length);
    // We set the EA bit to 0.
    octets[i] = ~kEAMask & (length << kLengthShift);
    length >>= 7;
  }
  // If we calculated the number of octets correctly above, then there should be
  // nothing remaining in length.
  FXL_DCHECK(length == 0);

  // Set the EA bit of the last octet to 1, to indicate it's the last octet.
  octets[octets.size() - 1] |= kEAMask;

  return octets;
}

// Compares |length| with the possible lengths for the information field of a
// multiplexer command of type |type|.
bool CommandLengthValid(MuxCommandType type, size_t length) {
  switch (type) {
    case MuxCommandType::kDLCParameterNegotiation:
      return length == kPNLength;
    case MuxCommandType::kTestCommand:
      // Any length is valid for a Test command.
      return true;
    case MuxCommandType::kFlowControlOnCommand:
      return length == kFConLength;
    case MuxCommandType::kFlowControlOffCommand:
      return length == kFCoffLength;
    case MuxCommandType::kModemStatusCommand:
      return length == kMSCWithBreakLength || length == kMSCWithoutBreakLength;
    case MuxCommandType::kNonSupportedCommandResponse:
      return length == kNSCLength;
    case MuxCommandType::kRemoteLineStatusCommand:
    case MuxCommandType::kRemotePortNegotiationCommand:
      // TODO(gusss): change when RLS/RPN implemented
      return false;
  }
  return false;
}

}  // namespace

std::unique_ptr<MuxCommand> MuxCommand::Parse(
    const common::ByteBuffer& buffer) {
  FXL_DCHECK(buffer.size() >= kMinHeaderSize)
      << "Buffer must contain at least a type and length octet.";

  CommandResponse command_response = (buffer[kTypeIndex] & kCRMask)
                                         ? CommandResponse::kCommand
                                         : CommandResponse::kResponse;
  MuxCommandType type = (MuxCommandType)(buffer[kTypeIndex] & kTypeMask);

  // Read the (potentially numerous) length octets.
  size_t length = 0, length_idx = kLengthIndex, num_length_octets = 0;
  do {
    // Shift right to shift out the EA bit, and then shift into place.
    length |= (buffer[length_idx] >> kLengthShift) << (7 * num_length_octets);
    ++num_length_octets;
  } while ((buffer[length_idx++] & kEAMask) == 0);
  // 7*num_length_octets is the number of bits encoded by length.
  // Here, we're limiting the size of the length field. In theory, the spec
  // allows the length field to be of any size, but here we're limiting it to
  // fitting in size_t.
  if (7 * num_length_octets > 8 * sizeof(size_t)) {
    FXL_LOG(WARNING)
        << "Encoded length is larger than the max value of size_t.";
    return nullptr;
  }

  // Check that the buffer is actually at least as big as the command which it
  // contains. Command is 1 control octet, multiple length octets, and payload.
  if (buffer.size() < 1 + num_length_octets + length) {
    FXL_LOG(WARNING) << "Buffer is shorter than the command it contains";
    return nullptr;
  }

  if (!CommandLengthValid(MuxCommandType(type), length)) {
    FXL_LOG(ERROR) << "Unexpected length " << length << " for multiplexer"
                   << " command of type " << unsigned(type);
    return nullptr;
  }

  switch (type) {
    case MuxCommandType::kDLCParameterNegotiation:
      return DLCParameterNegotiationCommand::Parse(command_response, buffer);

    case MuxCommandType::kTestCommand:
      return TestCommand::Parse(command_response, length, buffer);

    case MuxCommandType::kFlowControlOnCommand:
      return FlowControlOnCommand::Parse(command_response);

    case MuxCommandType::kFlowControlOffCommand:
      return FlowControlOffCommand::Parse(command_response);

    case MuxCommandType::kModemStatusCommand:
      return ModemStatusCommand::Parse(command_response, length, buffer);

    case MuxCommandType::kNonSupportedCommandResponse:
      return NonSupportedCommandResponse::Parse(command_response, buffer);

    default:
      FXL_LOG(WARNING) << "Unrecognized multiplexer command type: "
                       << unsigned(type);
  }

  return std::unique_ptr<MuxCommand>(nullptr);
}

TestCommand::TestCommand(CommandResponse command_response,
                         const common::ByteBuffer& test_pattern)
    : MuxCommand(MuxCommandType::kTestCommand, command_response) {
  test_pattern_ = common::DynamicByteBuffer(test_pattern.size());
  test_pattern.Copy(&test_pattern_, 0, test_pattern.size());
}

std::unique_ptr<TestCommand> TestCommand::Parse(
    CommandResponse command_response, size_t length,
    const common::ByteBuffer& buffer) {
  return std::make_unique<TestCommand>(command_response,
                                       buffer.view(2, length));
}

void TestCommand::Write(common::MutableBufferView buffer) const {
  FXL_CHECK(buffer.size() >= written_size());

  size_t idx = 0;

  buffer[idx] = type_field_octet();
  ++idx;

  // Write the length field octet(s). If the length fits in one byte (accounting
  // for the EA bit), we write it immediately. This should be the common case.
  if (test_pattern_.size() <= kMaxSingleOctetLength) {
    buffer[idx] = kEAMask | (test_pattern_.size() << kLengthShift);
    ++idx;
  } else {
    auto length_field_octets = CreateLengthFieldOctets(test_pattern_.size());
    buffer.Write(length_field_octets, idx);
    idx += length_field_octets.size();
  }

  buffer.Write(test_pattern_, idx);

  FXL_DCHECK(idx + test_pattern_.size() == buffer.size());
}

size_t TestCommand::written_size() const {
  return 1                                              // Type
         + NumLengthOctetsNeeded(test_pattern_.size())  // Length
         + test_pattern_.size();                        // Payload
}

std::unique_ptr<FlowControlOnCommand> FlowControlOnCommand::Parse(
    CommandResponse command_response) {
  return std::make_unique<FlowControlOnCommand>(command_response);
}

void FlowControlOnCommand::Write(common::MutableBufferView buffer) const {
  FXL_CHECK(buffer.size() >= written_size());
  buffer[kTypeIndex] = type_field_octet();
  // Length = 0, EA bit = 1.
  buffer[kLengthIndex] = kFlowcontrolOnLength;
}

size_t FlowControlOnCommand::written_size() const { return 2ul + kFConLength; }

std::unique_ptr<FlowControlOffCommand> FlowControlOffCommand::Parse(
    CommandResponse command_response) {
  return std::make_unique<FlowControlOffCommand>(command_response);
}

void FlowControlOffCommand::Write(common::MutableBufferView buffer) const {
  FXL_CHECK(buffer.size() >= written_size());
  buffer[kTypeIndex] = type_field_octet();
  // Length = 0, EA bit = 1.
  buffer[kLengthIndex] = kFlowcontrolOffLength;
}

size_t FlowControlOffCommand::written_size() const { return 2ul + kFConLength; }

std::unique_ptr<ModemStatusCommand> ModemStatusCommand::Parse(
    CommandResponse command_response, size_t length,
    const common::ByteBuffer& buffer) {
  DLCI dlci = buffer[2] >> kMSCDLCIShift;
  ModemStatusCommandSignals signals;
  BreakValue break_value = kDefaultInvalidBreakValue;

  // The first bit of |buffer[4]| encodes whether the octet encodes a break
  // signal. If it does not, then we let break_value default to an invalid
  // value.
  if (length == kMSCWithBreakLength && buffer[4] & kMSCBreakSignalMask) {
    break_value = buffer[4] >> kMSCBreakValueShift;
  }

  // clang-format off
  signals.flow_control          = buffer[3] & kMSCFlowControlMask;
  signals.ready_to_communicate  = buffer[3] & kMSCReadyToCommunicateMask;
  signals.ready_to_receive      = buffer[3] & kMSCReadyToReceiveMask;
  signals.incoming_call         = buffer[3] & kMSCIncomingCallMask;
  signals.data_valid            = buffer[3] & kMSCDataValidMask;
  // clang-format on

  return std::make_unique<ModemStatusCommand>(command_response, dlci, signals,
                                              break_value);
}

void ModemStatusCommand::Write(common::MutableBufferView buffer) const {
  FXL_CHECK(buffer.size() >= written_size());
  buffer[kTypeIndex] = type_field_octet();
  // EA bit = 1.
  buffer[kLengthIndex] =
      ((has_break_signal() ? kMSCWithBreakLength : kMSCWithoutBreakLength)
       << kLengthShift) |
      kEAMask;
  // EA bit = 1, bit 2 = 1.
  buffer[2] = kEAMask | (1 << 1) | (dlci_ << kMSCDLCIShift);
  // clang-format off
  buffer[3] = !has_break_signal()
              | signals_.flow_control             << kMSCFlowControlShift
              | signals_.ready_to_communicate     << kMSCReadyToCommunicateShift
              | signals_.ready_to_receive         << kMSCReadyToReceiveShift
              | signals_.incoming_call            << kMSCIncomingCallShift
              | signals_.data_valid               << kMSCDataValidShift;
  // clang-format on
  if (has_break_signal()) {
    // clang-format off
    buffer[4] = kEAMask
                | has_break_signal()  << kMSCBreakSignalShift
                | break_value_        << kMSCBreakValueShift;
    // clang-format on
  }
}

size_t ModemStatusCommand::written_size() const {
  return 2ul +
         (has_break_signal() ? kMSCWithBreakLength : kMSCWithoutBreakLength);
}

std::unique_ptr<NonSupportedCommandResponse> NonSupportedCommandResponse::Parse(
    CommandResponse command_response, const common::ByteBuffer& buffer) {
  CommandResponse incoming_command_response = buffer[2] & kCRMask
                                                  ? CommandResponse::kCommand
                                                  : CommandResponse::kResponse;
  uint8_t incoming_non_supported_command =
      buffer[2] >> kNSCNotSupportedCommandShift;

  return std::make_unique<NonSupportedCommandResponse>(
      incoming_command_response, incoming_non_supported_command);
}

void NonSupportedCommandResponse::Write(
    common::MutableBufferView buffer) const {
  FXL_CHECK(buffer.size() >= written_size());
  buffer[kTypeIndex] = type_field_octet();
  // EA bit = 1.
  buffer[kLengthIndex] = (kNSCLength << kLengthShift) | kEAMask;
  buffer[2] = kEAMask |
              (incoming_command_response_ == CommandResponse::kCommand ? 1 : 0)
                  << kNSCCRShift |
              incoming_non_supported_command_ << kNSCNotSupportedCommandShift;
}

size_t NonSupportedCommandResponse::written_size() const {
  return 2ul + kNSCLength;
}

std::unique_ptr<DLCParameterNegotiationCommand>
DLCParameterNegotiationCommand::Parse(CommandResponse command_response,
                                      const common::ByteBuffer& buffer) {
  ParameterNegotiationParams params;

  params.dlci = buffer[2];
  params.credit_based_flow_handshake = static_cast<CreditBasedFlowHandshake>(
      buffer[3] >> kPNCreditBasedFlowHandshakeShift);
  params.priority = buffer[4];
  params.maximum_frame_size = buffer[6] | buffer[7] << 8;
  params.initial_credits = buffer[9];

  return std::make_unique<DLCParameterNegotiationCommand>(command_response,
                                                          params);
}

void DLCParameterNegotiationCommand::Write(
    common::MutableBufferView buffer) const {
  FXL_CHECK(buffer.size() >= written_size());
  buffer[kTypeIndex] = type_field_octet();
  // EA bit = 1.
  buffer[kLengthIndex] = (kPNLength << kLengthShift) | kEAMask;

  buffer[2] = params_.dlci & kPNDLCIMask;
  buffer[3] = (uint8_t)params_.credit_based_flow_handshake
              << kPNCreditBasedFlowHandshakeShift;
  buffer[4] = params_.priority & kPNPriorityMask;
  buffer[5] = 0;
  buffer[6] = (uint8_t)params_.maximum_frame_size;
  buffer[7] = (uint8_t)(params_.maximum_frame_size >> 8);
  buffer[8] = 0;
  buffer[9] = params_.initial_credits & kPNInitialCreditsMask;
}

size_t DLCParameterNegotiationCommand::written_size() const {
  return 2ul + kPNLength;
}

}  // namespace rfcomm
}  // namespace btlib
