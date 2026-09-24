#include "NfcReader.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <string.h>

namespace {
constexpr uint8_t NFC_IRQ = 17;
constexpr uint8_t NFC_RESET = 45;
constexpr uint8_t NFC_I2C_ADDRESS = 0x24;
constexpr uint8_t UID_BUFFER_LEN = 7;
constexpr uint8_t TYPE2_DATA_START_PAGE = 4;
constexpr uint8_t TYPE2_MAX_PAGE = 20;
constexpr size_t TYPE2_MAX_BYTES = 80;
constexpr uint32_t CARD_LOST_TIMEOUT_MS = 1500;
constexpr uint32_t RESET_SETTLE_MS = 40;
constexpr uint32_t REARM_INTERVAL_MS = 30;
constexpr uint32_t BUS_SETTLE_MS = 10;
constexpr uint32_t ACK_TIMEOUT_MS = 120;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 120;

Adafruit_PN532 *reader = nullptr;
bool active = false;
bool reader_ready = false;
bool detection_armed = false;
bool card_present = false;
uint32_t last_seen_at = 0;
uint32_t last_arm_attempt_at = 0;
char current_uid[32] = {};

void uid_to_hex(const uint8_t *uid, uint8_t uid_length, char *out, size_t out_len) {
  out[0] = '\0';
  for (uint8_t i = 0; i < uid_length && strlen(out) + 4 < out_len; ++i) {
    char part[4];
    snprintf(part, sizeof(part), "%s%02X", i == 0 ? "" : " ", uid[i]);
    strlcat(out, part, out_len);
  }
}

void card_id_from_uid(const uint8_t *uid, uint8_t uid_length, char *out, size_t out_len) {
  if (uid_length == 4) {
    snprintf(out, out_len, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
  } else {
    strlcpy(out, "-", out_len);
  }
}

const char *uid_type(uint8_t uid_length) {
  if (uid_length == 4) return "ISO14443A 4B UID";
  if (uid_length == 7) return "ISO14443A 7B UID";
  return "ISO14443A";
}

bool wait_for_irq(uint32_t timeout_ms) {
  const uint32_t start_ms = millis();
  while (digitalRead(NFC_IRQ) != LOW) {
    if (millis() - start_ms > timeout_ms) return false;
    delay(1);
  }
  return true;
}

bool read_ack_frame() {
  uint8_t ack_frame[7] = {};
  size_t read_count = Wire.requestFrom((uint8_t)NFC_I2C_ADDRESS,
                                       (uint8_t)sizeof(ack_frame),
                                       (uint8_t)true);
  if (read_count != sizeof(ack_frame)) return false;
  for (uint8_t i = 0; i < sizeof(ack_frame); ++i) ack_frame[i] = Wire.read();

  static constexpr uint8_t expected_ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  return ack_frame[0] == 0x01 && memcmp(ack_frame + 1, expected_ack, sizeof(expected_ack)) == 0;
}

bool read_response(uint8_t expected_response_code, size_t frame_size,
                   uint8_t *payload, size_t payload_capacity, size_t &payload_length) {
  uint8_t frame[64] = {};
  if (frame_size > sizeof(frame)) return false;
  size_t read_count = Wire.requestFrom((uint8_t)NFC_I2C_ADDRESS,
                                       (uint8_t)frame_size,
                                       (uint8_t)true);
  if (read_count != frame_size || read_count < 10) return false;
  for (size_t i = 0; i < read_count && i < sizeof(frame); ++i) frame[i] = Wire.read();

  if (frame[0] != 0x01) return false;
  if (frame[1] != 0x00 || frame[2] != 0x00 || frame[3] != 0xFF) return false;
  uint8_t length = frame[4];
  if ((uint8_t)(length + frame[5]) != 0) return false;
  if (length < 2 || size_t(length) + 7 > read_count) return false;
  if (frame[6] != 0xD5 || frame[7] != expected_response_code) return false;

  uint8_t checksum = 0;
  for (uint8_t i = 0; i < length; ++i) checksum += frame[6 + i];
  if ((uint8_t)(checksum + frame[6 + length]) != 0) return false;
  if (frame[7 + length] != 0x00) return false;

  payload_length = length - 2;
  if (payload_length > payload_capacity) payload_length = payload_capacity;
  memcpy(payload, frame + 8, payload_length);
  return true;
}

bool write_command_ack_only(const uint8_t *cmd, uint8_t cmd_len) {
  uint8_t packet[16] = {};
  uint8_t len = cmd_len + 1;
  uint8_t sum = 0;

  if (cmd_len > sizeof(packet) - 8) return false;
  packet[0] = 0x00;
  packet[1] = 0x00;
  packet[2] = 0xFF;
  packet[3] = len;
  packet[4] = (uint8_t)(~len + 1);
  packet[5] = 0xD4;
  for (uint8_t i = 0; i < cmd_len; ++i) {
    packet[6 + i] = cmd[i];
    sum += cmd[i];
  }
  packet[6 + cmd_len] = (uint8_t)(~(0xD4 + sum) + 1);
  packet[7 + cmd_len] = 0x00;

  Wire.beginTransmission(NFC_I2C_ADDRESS);
  Wire.write(packet, 8 + cmd_len);
  if (Wire.endTransmission() != 0) return false;
  if (!wait_for_irq(ACK_TIMEOUT_MS)) return false;
  return read_ack_frame();
}

bool send_command_with_response(const uint8_t *cmd, uint8_t cmd_len,
                                uint8_t expected_response_code, size_t frame_size,
                                uint8_t *payload, size_t payload_capacity,
                                size_t &payload_length) {
  if (!write_command_ack_only(cmd, cmd_len)) return false;
  if (!wait_for_irq(RESPONSE_TIMEOUT_MS)) return false;
  return read_response(expected_response_code, frame_size, payload, payload_capacity,
                       payload_length);
}

bool configure_passive_activation_retries(uint8_t max_retries) {
  const uint8_t config_command[] = {0x32, 0x05, 0xFF, 0x01, max_retries};
  uint8_t payload[2] = {};
  size_t payload_length = 0;
  return send_command_with_response(config_command, sizeof(config_command), 0x33, 10,
                                    payload, sizeof(payload), payload_length);
}

bool arm_passive_detection() {
  if (!reader_ready || detection_armed) return reader_ready;

  const uint8_t detect_command[] = {0x4A, 0x01, PN532_MIFARE_ISO14443A};
  last_arm_attempt_at = millis();
  if (!write_command_ack_only(detect_command, sizeof(detect_command))) return false;
  detection_armed = true;
  return true;
}

bool read_type2_page(uint8_t page, uint8_t *data) {
  const uint8_t command[] = {0x40, 0x01, MIFARE_CMD_READ, page};
  uint8_t payload[18] = {};
  size_t payload_length = 0;
  if (!send_command_with_response(command, sizeof(command), 0x41, 27,
                                  payload, sizeof(payload), payload_length)) {
    return false;
  }
  if (payload_length < 17 || payload[0] != 0x00) return false;
  memcpy(data, payload + 1, 16);
  return true;
}

size_t read_type2_user_data(uint8_t *data, size_t data_capacity) {
  if (data_capacity < 16) return 0;

  uint8_t block[16] = {};
  if (!read_type2_page(TYPE2_DATA_START_PAGE, block)) return 0;
  memcpy(data, block, sizeof(block));
  size_t offset = sizeof(block);
  size_t needed = offset;

  for (size_t pos = 0; pos + 1 < offset; ++pos) {
    uint8_t tlv = data[pos];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) return offset;
    if (tlv == 0x03) {
      size_t length_pos = pos + 1;
      if (data[length_pos] == 0xFF) {
        if (length_pos + 2 >= offset) break;
        needed = length_pos + 3 + ((size_t(data[length_pos + 1]) << 8) |
                                   data[length_pos + 2]);
      } else {
        needed = length_pos + 1 + data[length_pos];
      }
      if (needed < data_capacity) ++needed; // include possible terminator byte
      break;
    }
  }

  if (needed > data_capacity) needed = data_capacity;
  for (uint8_t page = TYPE2_DATA_START_PAGE + 4;
       page <= TYPE2_MAX_PAGE && offset < needed;
       page += 4) {
    if (!read_type2_page(page, block)) break;
    size_t copy_len = min((size_t)sizeof(block), data_capacity - offset);
    memcpy(data + offset, block, copy_len);
    offset += copy_len;
    for (size_t i = 0; i < copy_len; ++i) {
      if (block[i] == 0xFE) return offset;
    }
  }
  return offset;
}

bool copy_printable_ascii(const uint8_t *data, size_t length, char *out, size_t out_len) {
  for (size_t i = 0; i + 4 < length; ++i) {
    if (data[i] != 'T') continue;
    uint8_t status = data[i + 1];
    uint8_t lang_length = status & 0x3F;
    if ((status & 0x80) || lang_length == 0 || lang_length > 8) continue;
    if (i + 2 + lang_length >= length) continue;

    bool language_is_ascii = true;
    for (uint8_t j = 0; j < lang_length; ++j) {
      uint8_t c = data[i + 2 + j];
      if (c < 'a' || c > 'z') language_is_ascii = false;
    }
    if (!language_is_ascii) continue;

    size_t text_pos = i + 2 + lang_length;
    size_t out_pos = 0;
    while (text_pos < length && out_pos + 1 < out_len && data[text_pos] != 0xFE) {
      if (data[text_pos] >= 32 && data[text_pos] <= 126) out[out_pos++] = (char)data[text_pos];
      ++text_pos;
    }
    out[out_pos] = '\0';
    return out_pos > 0;
  }

  size_t out_pos = 0;
  for (size_t i = 0; i < length && out_pos + 1 < out_len; ++i) {
    if (data[i] >= 32 && data[i] <= 126) out[out_pos++] = (char)data[i];
    else if ((data[i] == '\n' || data[i] == '\r' || data[i] == '\t') &&
             out_pos + 1 < out_len) {
      out[out_pos++] = ' ';
    }
  }
  out[out_pos] = '\0';
  return out_pos > 0;
}

bool parse_ndef_text_record(const uint8_t *data, size_t length, char *out, size_t out_len) {
  size_t pos = 0;
  while (pos < length) {
    uint8_t tlv = data[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) break;
    if (pos >= length) break;

    size_t tlv_length = data[pos++];
    if (tlv_length == 0xFF) {
      if (pos + 1 >= length) break;
      tlv_length = (size_t(data[pos]) << 8) | data[pos + 1];
      pos += 2;
    }
    size_t tlv_end = pos + tlv_length;
    if (tlv_end > length) tlv_end = length;
    if (tlv != 0x03) {
      pos = tlv_end;
      continue;
    }

    size_t end = tlv_end;
    while (pos < end) {
      uint8_t header = data[pos++];
      bool short_record = header & 0x10;
      bool id_length_present = header & 0x08;
      uint8_t tnf = header & 0x07;
      if (pos >= end) break;

      uint8_t type_length = data[pos++];
      size_t payload_length = 0;
      if (short_record) {
        if (pos >= end) break;
        payload_length = data[pos++];
      } else {
        if (pos + 3 >= end) break;
        payload_length = (size_t(data[pos]) << 24) | (size_t(data[pos + 1]) << 16) |
                         (size_t(data[pos + 2]) << 8) | data[pos + 3];
        pos += 4;
      }
      uint8_t id_length = 0;
      if (id_length_present) {
        if (pos >= end) break;
        id_length = data[pos++];
      }
      if (pos + type_length + id_length > end) break;

      const uint8_t *type = data + pos;
      pos += type_length + id_length;
      const uint8_t *payload = data + pos;
      size_t available_payload = end - pos;
      if (payload_length > available_payload) payload_length = available_payload;
      pos += payload_length;

      if (tnf == 0x01 && type_length == 1 && type[0] == 'T' && payload_length >= 1) {
        uint8_t status = payload[0];
        uint8_t lang_length = status & 0x3F;
        bool utf16 = status & 0x80;
        if (utf16 || payload_length <= size_t(1 + lang_length)) return false;
        size_t text_length = payload_length - 1 - lang_length;
        if (text_length >= out_len) text_length = out_len - 1;
        memcpy(out, payload + 1 + lang_length, text_length);
        out[text_length] = '\0';
        return true;
      }
    }
    break;
  }
  return false;
}

bool read_tag_text(char *out, size_t out_len) {
  uint8_t data[TYPE2_MAX_BYTES] = {};
  size_t data_length = read_type2_user_data(data, sizeof(data));
  if (!data_length) return false;
  if (parse_ndef_text_record(data, data_length, out, out_len)) return true;
  return copy_printable_ascii(data, data_length, out, out_len);
}

bool init_reader() {
  pinMode(NFC_RESET, OUTPUT);
  digitalWrite(NFC_RESET, HIGH);
  pinMode(NFC_IRQ, INPUT_PULLUP);

  Wire.setPins(8, 18);
  Wire.setClock(100000U);
  Wire.setTimeOut(20);

  if (!reader->begin()) return false;
  if (!reader->getFirmwareVersion()) return false;
  if (!reader->SAMConfig()) return false;
  if (!configure_passive_activation_retries(0xFF)) return false;

  reader_ready = true;
  card_present = false;
  current_uid[0] = '\0';
  return arm_passive_detection();
}

void print_detected_tag(const uint8_t *uid, uint8_t uid_length) {
  char uid_text[sizeof(current_uid)] = {};
  char card_id[12] = {};
  uid_to_hex(uid, uid_length, uid_text, sizeof(uid_text));
  card_id_from_uid(uid, uid_length, card_id, sizeof(card_id));

  Serial.println(F("[NFC] Tag detected"));
  Serial.print(F("[NFC] UID: "));
  Serial.println(uid_text);
  Serial.print(F("[NFC] Type: "));
  Serial.println(uid_type(uid_length));
  Serial.print(F("[NFC] Card ID: "));
  Serial.println(card_id);
  char tag_text[80] = {};
  if (read_tag_text(tag_text, sizeof(tag_text))) {
    Serial.print(F("[NFC] Text: "));
    Serial.println(tag_text);
  } else {
    Serial.println(F("[NFC] Text: <none>"));
  }
}

void handle_detected_tag(const uint8_t *uid, uint8_t uid_length) {
  char detected_uid[sizeof(current_uid)] = {};
  uid_to_hex(uid, uid_length, detected_uid, sizeof(detected_uid));
  if (card_present && strcmp(detected_uid, current_uid) == 0) {
    last_seen_at = millis();
    return;
  }

  strlcpy(current_uid, detected_uid, sizeof(current_uid));
  card_present = true;
  last_seen_at = millis();
  print_detected_tag(uid, uid_length);
  reader_ready = false;
  detection_armed = false;
  digitalWrite(NFC_RESET, LOW);
}

void handle_card_timeout() {
  if (card_present && millis() - last_seen_at > CARD_LOST_TIMEOUT_MS) {
    card_present = false;
    current_uid[0] = '\0';
    digitalWrite(NFC_RESET, HIGH);
    delay(RESET_SETTLE_MS);
    init_reader();
  }
}
} // namespace

void nfc_reader_init() {
  static Adafruit_PN532 device(NFC_IRQ, NFC_RESET);
  reader = &device;
  pinMode(NFC_RESET, OUTPUT);
  digitalWrite(NFC_RESET, LOW);
  pinMode(NFC_IRQ, INPUT_PULLUP);
}

bool nfc_reader_start() {
  if (!reader) return false;
  if (active) return reader_ready;

  active = true;
  detection_armed = false;
  reader_ready = false;
  init_reader();
  return true;
}

void nfc_reader_update() {
  if (!active) return;

  const uint32_t now = millis();
  handle_card_timeout();
  if (!reader_ready) return;
  // Do not re-arm while a tag is considered present. Re-arming immediately
  // after a successful read can keep IRQ asserted and starve UI navigation.
  if (card_present) return;

  if (!detection_armed && now - last_arm_attempt_at >= REARM_INTERVAL_MS) {
    arm_passive_detection();
  }

  if (detection_armed && digitalRead(NFC_IRQ) == LOW) {
    uint8_t uid[UID_BUFFER_LEN] = {};
    uint8_t uid_length = 0;

    detection_armed = false;
    if (reader->readDetectedPassiveTargetID(uid, &uid_length) && uid_length > 0) {
      handle_detected_tag(uid, uid_length);
    }

    delay(BUS_SETTLE_MS);
  }
}

void nfc_reader_stop() {
  active = false;
  reader_ready = false;
  detection_armed = false;
  card_present = false;
  current_uid[0] = '\0';
  digitalWrite(NFC_RESET, LOW);
}
