// Log
//
// Versions:
//   v0.1 20220712 - Enhancements
//   v0.2 20221014 - Enhancements
//   v0.3 20241012 - Enhancements
//   v0.4 20260606 - LOG_DEBUG() fix
//   v0.5 20260608 - Minor fix

#include "Application.h"
#include "Log.h"

void LOG(const char *msg) {
  Serial.println(msg);
}

void LOGX(const char *msg) {
  Serial.print(msg);
}

void LOG(const char *msg1, const char *msg2) {
  Serial.print(msg1);
  Serial.println(msg2);
}

void LOG(const char *msg1, uint8_t ui) {
  Serial.print(msg1);
  Serial.println(ui);
}

void LOG(const char *msg1, int32_t ui) {
  Serial.print(msg1);
  Serial.println(ui);
}

void LOG(const char *msg1, ulong ul) {
  Serial.print(msg1);
  Serial.println(ul);
}

void LOG(const char *msg1, long l) {
  Serial.print(msg1);
  Serial.println(l);
}

void LOG(const char *msg1, bool b) {
  Serial.print(msg1);
  Serial.println(b);
}

void LOG(const char *msg1, String msg2) {
  Serial.print(msg1);
  Serial.println(msg2);
}

void LOG(const char *msg1, const char *msg2, const char *msg3) {
  Serial.print(msg1);
  Serial.print(msg2);
  Serial.println(msg3);
}

void LOG_HEX(uint8_t dig) {
  Serial.print(dig, HEX);
}

void LOG_ERROR(const char *msg) {
  Serial.print(F("ERROR:"));
  LOG(msg);
}

void LOG_ERROR(const char *msg1, String msg2) {
  Serial.print(F("ERROR:"));
  LOG(msg1, msg2);
}

void LOG_ERROR(const char *msg1, const char *msg2) {
  Serial.print(F("ERROR:"));
  LOG(msg1, msg2);
}

void LOG_ERROR(const char *msg1, const char *msg2, const char *msg3) {
  Serial.print(F("ERROR:"));
  LOG(msg1, msg2, msg3);
}

void LOG_DEBUG(const char *msg) {
  Serial.print(F("DEBUG:"));
  Serial.println(msg);
}
