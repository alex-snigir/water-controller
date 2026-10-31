#include "main.h"
#include <Adafruit_ADS1X15.h>
#include "Application.h"


Adafruit_ADS1115 ads;
boolean ads_enabled;

#define ADS_RETRY_INTERVAL 60000L // 1 minute
#define ADS_CONVERSION_TIMEOUT 50L // Millis. Single conversion takes ~8 ms at the default 128 SPS
unsigned long ads_init_lastAttempt = 0L;

/*
ADS1115:

Programmable Gain Amplifier (PGA)
Input range programmed control, 6 types input ranges:
 Note: Do not apply more than (VDD + 0.3) V to the analog inputs of the device.
 
 Gain:   Single-Ended inputs     |  Differential Inputs (FSR - Full Scale Range)
 x2/3:   GND ~ +6.144V (defalt)  |   ~ +/-6.144V (defalt)
   x1:   GND ~ +4.096V           |   ~ +/-4.096V         
   x2:   GND ~ +2.048V           |   ~ +/-2.048V         
   x4:   GND ~ +1.024V           |   ~ +/-1.024V         
   x8:   GND ~ +0.512V           |   ~ +/-0.512V         
  x16:   GND ~ +0.256V           |   ~ +/-0.256V
*/
void ads_init() {
  ads_init_lastAttempt = millis();
  ads_enabled = ads.begin();
  if (!ads_enabled) {
    Serial.println("Failed to initialize ADS1115.");
  }
  else {
    switch(ads.getGain()) {
    case GAIN_TWOTHIRDS:
      Serial.println("ADC Range: +/- 6.144V (1 bit = 3mV:ADS1015, 0.1875mV:ADS1115)");
      break;
    }
  }
}


// Retry the ADS1115 initialization once a minute while it is not available
void ads_handle() {
  if (!ads_enabled && (millis() - ads_init_lastAttempt >= ADS_RETRY_INTERVAL)) {
    Serial.println("ADS1115: retrying initialization...");
    ads_init();
  }
}


// Marks the ADS1115 as unavailable; ads_handle() will retry the initialization
static void ads_lost(const char *reason) {
  Serial.printf("ADS1115: %s, marked as not available\n", reason);
  ads_enabled = false;
  ads_init_lastAttempt = millis();
}


// The Adafruit library ignores I2C errors: a lost write leaves the previous conversion in place,
// and a lost read returns leftovers of its buffer (about 0 V), which looks like a valid sample.
static bool ads_write_register(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(ADS1X15_ADDRESS);
  Wire.write(reg);
  Wire.write((uint8_t)(value >> 8));
  Wire.write((uint8_t)(value & 0xFF));
  return Wire.endTransmission() == 0;
}


static bool ads_read_register(uint8_t reg, uint16_t *value) {
  Wire.beginTransmission(ADS1X15_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)ADS1X15_ADDRESS, (uint8_t)2) != 2) {
    return false;
  }
  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();
  *value = (msb << 8) | lsb;
  return true;
}


// Reads the channel voltage into *volts. Returns false if the ADS1115 is not available or not responding,
// or if an I2C transfer failed: that sample is skipped, the next one retries.
// Replaces ads.readADC_SingleEnded(), which waits for the conversion forever if the device disappears.
bool ads_voltage_chnnel_get(int channel, float *volts) {

  if (!ads_enabled || channel < 0 || channel > 3) {
    return false;
  }

  // Check that the device still acknowledges its address
  Wire.beginTransmission(ADS1X15_ADDRESS);
  if (Wire.endTransmission() != 0) {
    ads_lost("no response on I2C");
    return false;
  }

  // Single-shot, comparator off: the ALERT/RDY pin is not used
  uint16_t config = ADS1X15_REG_CONFIG_OS_SINGLE | ADS1X15_REG_CONFIG_MODE_SINGLE | ADS1X15_REG_CONFIG_CQUE_NONE |
                    ads.getGain() | ads.getDataRate() | MUX_BY_CHANNEL[channel];
  if (!ads_write_register(ADS1X15_REG_POINTER_CONFIG, config)) {
    Serial.println("ADS1115: config write failed, sample skipped");
    return false;
  }

  unsigned long start = millis();
  uint16_t status;
  do {
    if (millis() - start >= ADS_CONVERSION_TIMEOUT) {
      ads_lost("conversion timeout");
      return false;
    }
    delay(1);
    if (!ads_read_register(ADS1X15_REG_POINTER_CONFIG, &status)) {
      Serial.println("ADS1115: status read failed, sample skipped");
      return false;
    }
  } while (!(status & ADS1X15_REG_CONFIG_OS_NOTBUSY));

  uint16_t raw;
  if (!ads_read_register(ADS1X15_REG_POINTER_CONVERT, &raw)) {
    Serial.println("ADS1115: result read failed, sample skipped");
    return false;
  }

  int16_t adc = (int16_t)raw; // Readed counts
  *volts = ads.computeVolts(adc);

  Serial.print("ADS: AIN:"); 
  Serial.print(channel); 
  Serial.print(": "); 
  Serial.print(adc); 
  Serial.print("  "); 
  Serial.print(*volts);
  Serial.println("V ");

  return true;
}
