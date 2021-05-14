#include "../PluginStructs/P248_data_struct.h"

#ifdef USES_P248

# include "../Helpers/Convert.h"

struct AHTx_Status {
  inline AHTx_Status(uint8_t stat) : status(stat) {}

  inline bool valid() const { return status != 0xFF; }

  inline bool calibrated() const { return (status & (1 << 3)) != 0; }

  inline bool busy() const { return (status & (1 << 7)) != 0; }

  const uint8_t status;
};

AHTx_Device::AHTx_Device(uint8_t addr, AHTx_device_type type) :
  i2cAddress(addr),
  device_type(type),
  last_hum_val(0),
  last_temp_val(0) {}

String AHTx_Device::getDeviceName() const {
  switch(device_type) {
    case AHT10_DEVICE: return F("AHT10");
    case AHT20_DEVICE: return F("AHT20");
    case AHT21_DEVICE: return F("AHT21");
    default: return F("AHTx");
  }
}

bool AHTx_Device::initialize() {
  const uint8_t cmd_init = (device_type == AHT10_DEVICE) ? 0xE1 : 0xBE;
  return I2C_write16_reg(i2cAddress, cmd_init, 0x0800);
}

bool AHTx_Device::triggerMeasurement() {
  return I2C_write16_reg(i2cAddress, 0xAC, 0x3300); // measurement time takes over 80 msec
}

bool AHTx_Device::softReset() {
  return I2C_write8(i2cAddress, 0xBA); // soft reset takes less than 20 msec
}

uint8_t AHTx_Device::readStatus() {
  return I2C_read8(i2cAddress, NULL);
}

bool AHTx_Device::readData() {
  const uint8_t data_len = 6;

  // I2C_read8 len
  if (Wire.requestFrom(i2cAddress, data_len) == 0) {
    return false;
  }

  uint8_t data[data_len];
  for (uint8_t i = 0; i < data_len; ++i) {
    data[i] = Wire.read();
  }

  // check status
  AHTx_Status status = data[0];
  if (not (status.valid() and status.calibrated())) {
    return false;
  }

  // 20 bits humidity value
  uint32_t value = data[1];
  value = (value << 8) | data[2];
  value = (value << 4) | (data[3] >> 4);
  last_hum_val = (float)value / (1 << 20) * 100;

  // 20 bits temperature value
  value = data[3] & 0x0F;
  value = (value << 8) | data[4];
  value = (value << 8) | data[5];
  last_temp_val = (float)value / (1 << 20) * 200 - 50;

  return true;
}

P248_data_struct::P248_data_struct(uint8_t addr, AHTx_device_type dev) :
  device(addr, dev),
  state(AHTx_Uninitialized),
  last_measurement(0),
  trigger_time(0) {}

bool P248_data_struct::initialized() const {
  return state != AHTx_Uninitialized;
}

void P248_data_struct::setUninitialized() {
  state = AHTx_Uninitialized;
}

// Perform the measurements with interval
bool P248_data_struct::updateMeasurements(unsigned long task_index) {
  const unsigned long current_time = millis();

  if (!initialized()) {
    if (!device.initialize()) {
      addLog(LOG_LEVEL_DEBUG, getDeviceName() + F(" : unable to initialize"));
      return false;
    }
    addLog(LOG_LEVEL_INFO, getDeviceName() + F(" : initialized"));

    trigger_time = current_time;
    state = AHTx_Trigger_measurement;
    return false;
  }

  if (state != AHTx_Wait_for_samples and state != AHTx_Trigger_measurement) {
    if (!timeOutReached(last_measurement + (Settings.TaskDeviceTimer[task_index] * 1000))) {
      // Timeout has not yet been reached.
      return false;
    }
    trigger_time = current_time;
    state = AHTx_Trigger_measurement;
  }

  // state: AHTx_Wait_for_samples or AHTx_Trigger_measurement
  AHTx_Status status = device.readStatus();
  if (status.valid() and status.calibrated() and not status.busy()) {

    if (state == AHTx_Trigger_measurement) {
      device.triggerMeasurement();

      trigger_time = current_time;
      state = AHTx_Wait_for_samples;
      return false;
    }

    // state: AHTx_Wait_for_samples
    if (!device.readData()) {
      return false;
    }

    last_measurement = current_time;
    state = AHTx_New_values;

    if (loglevelActiveFor(LOG_LEVEL_INFO)) {
      String log;
      log.reserve(120); // Prevent re-allocation
      log  = getDeviceName();
      log += F(" : humidity ");
      log += device.getHumidity();
      log += F("% temperature ");
      log += device.getTemperature();
      log += F("C");
      addLog(LOG_LEVEL_INFO, log);
    }

    return true;
  }

  if (timePassedSince(trigger_time) > 1000) {
    // should not happen
    addLog(LOG_LEVEL_ERROR, getDeviceName() + F(" : reset"));
    device.softReset();

    state = AHTx_Uninitialized;
  }

  return false;
}

#endif // ifdef USES_P248
