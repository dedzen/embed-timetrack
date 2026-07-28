#include "BatteryGauge.h"
#include <Wire.h>

#define BOARD_I2C_SDA 8
#define BOARD_I2C_SCL 18
#define BQ27220_ADDR  0x55
#define BQ27220_CMD_STATE_OF_CHARGE 0x2C

bool battery_gauge_init() {
  return Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
}

int read_battery_percent() {
  Wire.beginTransmission(BQ27220_ADDR);
  Wire.write(BQ27220_CMD_STATE_OF_CHARGE);
  if (Wire.endTransmission(false) != 0) return -1; // repeated start, keep bus held

  if (Wire.requestFrom((int)BQ27220_ADDR, 2) != 2) return -1;
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  uint16_t soc = (hi << 8) | lo;

  if (soc > 100) return -1; // sanity check -- garbage read, not a real percentage
  return (int)soc;
}
