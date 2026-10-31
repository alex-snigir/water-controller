#include "main.h"
#include <Adafruit_ADS1X15.h>
#include "Application.h"


Adafruit_ADS1115 ads;
boolean ads_enabled;

#define ADS_RETRY_INTERVAL 60000L // 1 minute
#define ADS_I2C_ADDRESS ADS1X15_ADDRESS // 0x48: ADDR is tied to GND on the module
#define ADS_CONVERSION_MARGIN 40L  // Millis on top of the conversion time, ~8 ms at the default 128 SPS
#define ADS_MAX_FAILURES 3         // Failed samples in a row before the ADS1115 counts as lost
#define ADS_BUS_RECOVER_HALF_CLOCK 10 // Micros: 50 kHz, slower than the 100 kHz bus
unsigned long ads_init_lastAttempt = 0L;
static int ads_failures = 0;       // Failed samples in a row

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
void ads_debug_info() {
  Serial.println("\nads.cpp:\n========");
  Serial.printf("Available: %d\n", ads_enabled);
  Serial.printf("Failures in a row: %d of %d\n", ads_failures, ADS_MAX_FAILURES);
  if (!ads_enabled) {
    Serial.printf("Last init attempt: %lu ms ago\n", millis() - ads_init_lastAttempt);
  }
}


static void ads_start() {
  ads_init_lastAttempt = millis();
  ads_enabled = ads.begin(ADS_I2C_ADDRESS);
  if (!ads_enabled) {
    Serial.println("Failed to initialize ADS1115.");
  }
  else {
    Serial.printf("ADC full scale: %.3f V\n", ads.getFsRange());
  }
}


void ads_init() {
  Wire.begin(ADC_SDA_GPIO, ADC_SCL_GPIO);
  ads_start();
}


// ads.begin() cannot recover the bus: Wire.begin() returns at once if the bus is already started.
// A slave interrupted mid-byte holds SDA low until it gets the rest of the clocks, so that is done
// by hand, open-drain as the level shifter requires. Takes a few hundred microseconds.
static void ads_bus_recover() {
  Wire.end();

  digitalWrite(ADC_SDA_GPIO, HIGH);
  digitalWrite(ADC_SCL_GPIO, HIGH);
  pinMode(ADC_SDA_GPIO, OUTPUT_OPEN_DRAIN);
  pinMode(ADC_SCL_GPIO, OUTPUT_OPEN_DRAIN);

  for (int i = 0; i < 9; i++) {
    digitalWrite(ADC_SCL_GPIO, LOW);
    delayMicroseconds(ADS_BUS_RECOVER_HALF_CLOCK);
    digitalWrite(ADC_SCL_GPIO, HIGH);
    delayMicroseconds(ADS_BUS_RECOVER_HALF_CLOCK);
  }

  // STOP: SDA rises while SCL is high
  digitalWrite(ADC_SCL_GPIO, LOW);
  delayMicroseconds(ADS_BUS_RECOVER_HALF_CLOCK);
  digitalWrite(ADC_SDA_GPIO, LOW);
  delayMicroseconds(ADS_BUS_RECOVER_HALF_CLOCK);
  digitalWrite(ADC_SCL_GPIO, HIGH);
  delayMicroseconds(ADS_BUS_RECOVER_HALF_CLOCK);
  digitalWrite(ADC_SDA_GPIO, HIGH);
  delayMicroseconds(ADS_BUS_RECOVER_HALF_CLOCK);

  Wire.begin(ADC_SDA_GPIO, ADC_SCL_GPIO);

  // General call reset, for a chip that acknowledges but no longer converts.
  // The config is written on every sample, so nothing has to be restored.
  Wire.beginTransmission(0x00);
  Wire.write(0x06);
  Wire.endTransmission();
}


// Retry the ADS1115 initialization once a minute while it is not available
void ads_handle() {
  if (!ads_enabled && (millis() - ads_init_lastAttempt >= ADS_RETRY_INTERVAL)) {
    Serial.println("ADS1115: retrying initialization...");
    ads_bus_recover();
    ads_start();
  }
}


// Marks the ADS1115 as unavailable; ads_handle() will retry the initialization
static void ads_lost(const char *reason) {
  Serial.printf("ADS1115: %s, marked as not available\n", reason);
  ads_enabled = false;
  ads_failures = 0;
  ads_init_lastAttempt = millis();
}


// Valve motor noise can spoil a single transfer: only repeated failures mean the device is lost
static AdsResult ads_failed(const char *reason) {
  ads_failures++;
  if (ads_failures >= ADS_MAX_FAILURES) {
    ads_lost(reason);
    return ADS_UNAVAILABLE;
  }
  Serial.printf("ADS1115: %s, sample skipped (%d/%d)\n", reason, ads_failures, ADS_MAX_FAILURES);
  return ADS_SKIPPED;
}


// The Adafruit library ignores I2C errors: a lost write leaves the previous conversion in place,
// and a lost read returns leftovers of its buffer (about 0 V), which looks like a valid sample.
static bool ads_write_register(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(ADS_I2C_ADDRESS);
  Wire.write(reg);
  Wire.write((uint8_t)(value >> 8));
  Wire.write((uint8_t)(value & 0xFF));
  return Wire.endTransmission() == 0;
}


static bool ads_read_register(uint8_t reg, uint16_t *value) {
  Wire.beginTransmission(ADS_I2C_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)ADS_I2C_ADDRESS, (uint8_t)2) != 2) {
    return false;
  }
  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();
  *value = (msb << 8) | lsb;
  return true;
}


// Follows the configured data rate: at 8 SPS a conversion alone takes 125 ms
static unsigned long ads_conversion_timeout() {
  static const uint16_t SAMPLES_PER_SECOND[] = {8, 16, 32, 64, 128, 250, 475, 860}; // Indexed by the RATE_ADS1115_* bits
  return 1000 / SAMPLES_PER_SECOND[ads.getDataRate() >> 5] + ADS_CONVERSION_MARGIN;
}


// Reads the channel voltage into *volts. Returns ADS_SKIPPED if this sample failed and the next one retries,
// ADS_UNAVAILABLE if the ADS1115 is lost.
// Replaces ads.readADC_SingleEnded(), which waits for the conversion forever if the device disappears.
AdsResult ads_voltage_chnnel_get(int channel, float *volts) {

  if (!ads_enabled || channel < 0 || channel > 3) {
    return ADS_UNAVAILABLE;
  }

  // Single-shot, comparator off: the ALERT/RDY pin is not used
  uint16_t config = ADS1X15_REG_CONFIG_OS_SINGLE | ADS1X15_REG_CONFIG_MODE_SINGLE | ADS1X15_REG_CONFIG_CQUE_NONE |
                    ads.getGain() | ads.getDataRate() | MUX_BY_CHANNEL[channel];
  if (!ads_write_register(ADS1X15_REG_POINTER_CONFIG, config)) {
    return ads_failed("config write failed");
  }

  unsigned long timeout = ads_conversion_timeout();
  unsigned long start = millis();
  while (true) {
    delay(1);
    uint16_t status;
    if (!ads_read_register(ADS1X15_REG_POINTER_CONFIG, &status)) {
      return ads_failed("status read failed");
    }
    if (status & ADS1X15_REG_CONFIG_OS_NOTBUSY) {
      break;
    }
    // Checked after the status: a task switch here must not fail a conversion that has finished
    if (millis() - start >= timeout) {
      return ads_failed("conversion timeout");
    }
  }

  uint16_t raw;
  if (!ads_read_register(ADS1X15_REG_POINTER_CONVERT, &raw)) {
    return ads_failed("result read failed");
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

  ads_failures = 0;
  return ADS_OK;
}
