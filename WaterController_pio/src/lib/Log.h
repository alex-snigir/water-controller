#pragma once

void LOG(const char *msg);
void LOGX(const char *msg);
void LOG(const char *msg1, String msg2);
void LOG(const char *msg1, uint8_t ui);
void LOG(const char *msg1, int32_t ui);
void LOG(const char *msg1, ulong ul);
void LOG(const char *msg1, long l);
void LOG(const char *msg1, bool b);
void LOG(const char *msg1, const char *msg2);
void LOG(const char *msg1, const char *msg2, const char *msg3);

void LOG_HEX(uint8_t dig);

void LOG_ERROR(const char *msg);
void LOG_ERROR(const char *msg1, String msg2);
void LOG_ERROR(const char *msg1, const char *msg2);
void LOG_ERROR(const char *msg1, const char *msg2, const char *msg3);

void LOG_DEBUG(const char *msg);
