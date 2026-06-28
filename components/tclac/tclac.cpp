/**
 * Create by Miguel Ángel López on 20/07/19
 * and modify by xaxexa
 * Refactoring & component making:
 * Соловей с паяльником 15.03.2024
 *
 * Extended with diagnostic sensors by Anaximelis on 28.06.2026:
 *   - Turbo mode binary sensor            (dataRX[7] bit 7 = 0x80)
 *   - 0.5 °C target temperature precision (dataRX[9] bit 1 = 0x02)
 *   - Additional diagnostic sensors       (bytes 34–59)
 **/
#include "esphome.h"
#include "esphome/core/defines.h"
#include "tclac.h"

namespace esphome {
namespace tclac {

ClimateTraits tclacClimate::traits() {
    auto traits = climate::ClimateTraits();
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE); // Предудущие методы запрещены, теперь нужно использовать add_feature_flags
    // Previous methods are deprecated — use add_feature_flags now

    // Ответственно заявляю, что это все я взял у christoph5180
    // Credit for the following fallback pattern: christoph5180
    if (this->supported_modes_.empty()) {
        traits.add_supported_mode(climate::CLIMATE_MODE_OFF);  // Выключенный режим кондиционера доступен всегда
        // OFF mode is always available
        traits.add_supported_mode(climate::CLIMATE_MODE_AUTO); // Автоматический режим кондиционера тоже
        // AUTO mode is always available too
    } else {
        for (auto mode : this->supported_modes_)
            traits.add_supported_mode(mode);
    }
    if (this->supported_presets_.empty()) {
        traits.add_supported_preset(ClimatePreset::CLIMATE_PRESET_NONE); // На всякий случай без предустановок
        // No preset by default, just in case
    } else {
        for (auto preset : this->supported_presets_)
            traits.add_supported_preset(preset);
    }
    if (this->supported_fan_modes_.empty()) {
        traits.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO); // Автоматический режим вентилятора доступен всегда
        // AUTO fan mode is always available
    } else {
        for (auto fan_mode : this->supported_fan_modes_)
            traits.add_supported_fan_mode(fan_mode);
    }
    if (this->supported_swing_modes_.empty()) {
        traits.add_supported_swing_mode(climate::CLIMATE_SWING_OFF); // Выключенный режим качания заслонок доступен всегда
        // SWING OFF mode is always available
    } else {
        for (auto swing_mode : this->supported_swing_modes_)
            traits.add_supported_swing_mode(swing_mode);
    }
    return traits;
}

void tclacClimate::setup() {
#ifdef CONF_RX_LED
    this->rx_led_pin_->setup();
    this->rx_led_pin_->digital_write(false);
#endif
#ifdef CONF_TX_LED
    this->tx_led_pin_->setup();
    this->tx_led_pin_->digital_write(false);
#endif
}

void tclacClimate::loop() {
    // Если в буфере UART что-то есть, то читаем это что-то
    // If there is something in the UART buffer, read it
    if (esphome::uart::UARTDevice::available() > 0) {
        dataShow(0, true);
        dataRX[0] = esphome::uart::UARTDevice::read();
        // Если принятый байт- не заголовок (0xBB), то просто покидаем цикл
        // If the received byte is not the header (0xBB), just leave the loop
        if (dataRX[0] != 0xBB) {
            ESP_LOGD("TCL_EXT", "Wrong byte");
            dataShow(0, 0);
            return;
        }
        // А вот если совпал заголовок (0xBB), то начинаем чтение по цепочке еще 4 байт
        // If header matched (0xBB), start reading the next 4 bytes in sequence
        // Иногда, для некоторых кондиционеров все же нужно добавить delay(5) между пакетами. Зачем- ХЗ, но так надо. Но не всегда. Хотя иногда- да. Но не каждый раз. Изредка. Случается.
        // For some AC units a delay(5) between bytes is needed. Why? No idea, but it helps. Sometimes. Not always. Occasionally.
        // delay(5);
        dataRX[1] = esphome::uart::UARTDevice::read();
        // delay(5);
        dataRX[2] = esphome::uart::UARTDevice::read();
        // delay(5);
        dataRX[3] = esphome::uart::UARTDevice::read();
        // delay(5);
        dataRX[4] = esphome::uart::UARTDevice::read();

        // Из первых 5 байт нам нужен пятый- он содержит длину сообщения
        // Of the first 5 bytes we need the fifth — it contains the message length
        esphome::uart::UARTDevice::read_array(dataRX + 5, dataRX[4] + 1);

        // Проверяем контрольную сумму
        // Verify the checksum
        uint8_t check = getChecksum(dataRX, sizeof(dataRX));
        if (check != dataRX[60]) {
            ESP_LOGD("TCL_EXT", "Invalid checksum %x", check);
            dataShow(0, 0);
            return;
        }
        dataShow(0, 0);
        // Прочитав все из буфера приступаем к разбору данных
        // Having read everything from the buffer, proceed to parse the data
        readData();
    }
}

void tclacClimate::update() {
    dataShow(1, 1);
    this->esphome::uart::UARTDevice::write_array(poll, sizeof(poll));
    dataShow(1, 0);
}

void tclacClimate::readData() {
    // --- Temperatures ---
    current_temperature = float(((dataRX[17] << 8 | dataRX[18]) / 374 - 32) / 1.8);

    // Target temperature with 0.5 °C precision (bit 1 of byte 9, per adaasch/AC-hack)
    target_temperature = (dataRX[FAN_SPEED_POS] & SET_TEMP_MASK) + 16;
    if (dataRX[9] & DECI_TEMP_BIT)
        target_temperature += 0.5f;

    // --- Turbo mode sensor (bit 7 of byte 7) ---
    if (turbo_sensor_ != nullptr)
        turbo_sensor_->publish_state((dataRX[7] & TURBO_BIT) != 0);

    // --- Operating modes ---
    // Если кондиционер включен, то разбираем данные для отображения
    // If the AC is on, parse data for display
    if (dataRX[MODE_POS] & (1 << 4)) {
        uint8_t modeswitch     = MODE_MASK      & dataRX[MODE_POS];
        uint8_t fanspeedswitch = FAN_SPEED_MASK & dataRX[FAN_SPEED_POS];
        uint8_t swingswitch    = SWING_MODE_MASK & dataRX[SWING_POS];

        switch (modeswitch) {
            case MODE_AUTO:     mode = climate::CLIMATE_MODE_AUTO;     break;
            case MODE_COOL:     mode = climate::CLIMATE_MODE_COOL;     break;
            case MODE_DRY:      mode = climate::CLIMATE_MODE_DRY;      break;
            case MODE_FAN_ONLY: mode = climate::CLIMATE_MODE_FAN_ONLY; break;
            case MODE_HEAT:     mode = climate::CLIMATE_MODE_HEAT;     break;
            default:            mode = climate::CLIMATE_MODE_AUTO;
        }

        if (dataRX[FAN_QUIET_POS] & FAN_QUIET) {
            fan_mode = climate::CLIMATE_FAN_QUIET;
        } else if (dataRX[MODE_POS] & FAN_DIFFUSE) {
            fan_mode = climate::CLIMATE_FAN_DIFFUSE;
        } else {
            switch (fanspeedswitch) {
                case FAN_AUTO:   fan_mode = climate::CLIMATE_FAN_AUTO;   break;
                case FAN_LOW:    fan_mode = climate::CLIMATE_FAN_LOW;    break;
                case FAN_MIDDLE: fan_mode = climate::CLIMATE_FAN_MIDDLE; break;
                case FAN_MEDIUM: fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
                case FAN_HIGH:   fan_mode = climate::CLIMATE_FAN_HIGH;   break;
                case FAN_FOCUS:  fan_mode = climate::CLIMATE_FAN_FOCUS;  break;
                default:         fan_mode = climate::CLIMATE_FAN_AUTO;
            }
        }

        switch (swingswitch) {
            case SWING_OFF:        swing_mode = climate::CLIMATE_SWING_OFF;        break;
            case SWING_HORIZONTAL: swing_mode = climate::CLIMATE_SWING_HORIZONTAL; break;
            case SWING_VERTICAL:   swing_mode = climate::CLIMATE_SWING_VERTICAL;   break;
            case SWING_BOTH:       swing_mode = climate::CLIMATE_SWING_BOTH;       break;
        }

        // Обработка данных о пресете
        // Parse preset data
        preset = ClimatePreset::CLIMATE_PRESET_NONE;
        if      (dataRX[7]  & (1 << 6)) preset = ClimatePreset::CLIMATE_PRESET_ECO;
        else if (dataRX[9]  & (1 << 2)) preset = ClimatePreset::CLIMATE_PRESET_COMFORT;
        else if (dataRX[19] & (1 << 0)) preset = ClimatePreset::CLIMATE_PRESET_SLEEP;

    } else {
        // Если кондиционер выключен, то все режимы показываются, как выключенные
        // If the AC is off, all modes are shown as off
        mode       = climate::CLIMATE_MODE_OFF;
        swing_mode = climate::CLIMATE_SWING_OFF;
        preset     = ClimatePreset::CLIMATE_PRESET_NONE;
    }

    // --- Decoded diagnostic sensors (bytes 34–59) ---
    // Bytes 36–37: evaporator temperature (same formula as bytes 17–18)
    if (evap_temp_sensor_ != nullptr) {
        float evap = float((dataRX[36] << 8 | dataRX[37]) / 374.0 - 32) / 1.8f;
        evap_temp_sensor_->publish_state(evap);
    }

    // Byte 38: compressor temperature in °C (0 = outdoor unit idle)
    if (compressor_temp_sensor_ != nullptr)
        compressor_temp_sensor_->publish_state(dataRX[38] > 0 ? float(dataRX[38]) : NAN);

    // Byte 39: compressor frequency in Hz (0 = outdoor unit idle)
    if (compressor_freq_sensor_ != nullptr)
        compressor_freq_sensor_->publish_state(dataRX[39] > 0 ? float(dataRX[39]) : NAN);

    // Byte 45: mains voltage in V (direct reading, typically 214–220 V; only valid while compressor runs)
    if (voltage_sensor_ != nullptr && dataRX[46] != 0)
        voltage_sensor_->publish_state(float(dataRX[45]));

    // Byte 46: compressor running (0 = off, >0 = on)
    if (compressor_sensor_ != nullptr)
        compressor_sensor_->publish_state(dataRX[46] != 0);

    // Hex dump of remaining/not-yet-decoded bytes 34–59
    if (diag_hex_sensor_ != nullptr) {
        char buf[79];  // 26 bytes * 3 chars = 78 + null terminator
        char *ptr = buf;
        for (int i = 34; i <= 59; i++) {
            ptr += sprintf(ptr, "%02X", dataRX[i]);
            if (i < 59) *ptr++ = ' ';
        }
        *ptr = '\0';
        diag_hex_sensor_->publish_state(buf);
    }

    // Публикуем данные
    // Publish state to Home Assistant
    this->publish_state();
    allow_take_control = true;
}

// Climate control
void tclacClimate::control(const climate::ClimateCall &call) {
    ESP_LOGD("TCL", "Call from UI");

    // А это и ниже я подрезал у Vi3jo.
    // The following pattern was taken from Vi3jo.
    if (call.get_mode().has_value())              this->mode             = *call.get_mode();
    if (call.get_target_temperature().has_value()) this->target_temperature = *call.get_target_temperature();
    if (call.get_fan_mode().has_value())          this->fan_mode         = *call.get_fan_mode();
    if (call.get_swing_mode().has_value())        this->swing_mode       = *call.get_swing_mode();
    if (call.get_preset().has_value())            this->preset           = *call.get_preset();

    this->publish_state();
    this->takeControl();
    this->allow_take_control = true;
}

void tclacClimate::takeControl() {
    dataTX[7]  = 0;
    dataTX[8]  = 0;
    dataTX[9]  = 0;
    dataTX[10] = 0;
    dataTX[11] = 0;
    dataTX[19] = 0;
    dataTX[32] = 0;
    dataTX[33] = 0;

    uint8_t target_temperature_set = 31 - (int)target_temperature;

    // Включаем или отключаем пищалку в зависимости от переключателя в настройках
    // Enable or disable beeper based on the settings switch
    if (beeper_status_)  dataTX[7] += 0b00100000;
    // Включаем или отключаем дисплей на кондиционере в зависимости от переключателя в настройках
    // Enable or disable the AC display based on the settings switch
    // Включаем дисплей только если кондиционер в одном из рабочих режимов
    // Display is only enabled if the AC is in an active mode
    // ВНИМАНИЕ! При выключении дисплея кондиционер сам принудительно переходит в автоматический режим!
    // WARNING! Turning off the display forces the AC into AUTO mode!
    if (display_status_ && this->mode != climate::CLIMATE_MODE_OFF)
        dataTX[7] += 0b01000000;

    // Настраиваем режим работы кондиционера
    // Set AC operating mode
    switch (this->mode) {
        case climate::CLIMATE_MODE_OFF:
            break;
        case climate::CLIMATE_MODE_AUTO:
            dataTX[7] += 0b00000100; dataTX[8] += 0b00001000; break;
        case climate::CLIMATE_MODE_COOL:
            dataTX[7] += 0b00000100; dataTX[8] += 0b00000011; break;
        case climate::CLIMATE_MODE_DRY:
            dataTX[7] += 0b00000100; dataTX[8] += 0b00000010; break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            dataTX[7] += 0b00000100; dataTX[8] += 0b00000111; break;
        case climate::CLIMATE_MODE_HEAT:
            dataTX[7] += 0b00000100; dataTX[8] += 0b00000001; break;
    }

    // Настраиваем режим вентилятора
    // Set fan mode
    if (this->fan_mode.has_value()) {
        switch (*this->fan_mode) {
            case climate::CLIMATE_FAN_AUTO:    dataTX[8] += 0b00000000; dataTX[10] += 0b00000000; break;
            case climate::CLIMATE_FAN_QUIET:   dataTX[8] += 0b10000000; dataTX[10] += 0b00000000; break;
            case climate::CLIMATE_FAN_LOW:     dataTX[8] += 0b00000000; dataTX[10] += 0b00000001; break;
            case climate::CLIMATE_FAN_MIDDLE:  dataTX[8] += 0b00000000; dataTX[10] += 0b00000110; break;
            case climate::CLIMATE_FAN_MEDIUM:  dataTX[8] += 0b00000000; dataTX[10] += 0b00000011; break;
            case climate::CLIMATE_FAN_HIGH:    dataTX[8] += 0b00000000; dataTX[10] += 0b00000111; break;
            case climate::CLIMATE_FAN_FOCUS:   dataTX[8] += 0b00000000; dataTX[10] += 0b00000101; break;
            case climate::CLIMATE_FAN_DIFFUSE: dataTX[8] += 0b01000000; dataTX[10] += 0b00000000; break;
        }
    }

    // Устанавливаем режим качания заслонок
    // Set vane swing mode
    switch (this->swing_mode) {
        case climate::CLIMATE_SWING_OFF:
            break;
        case climate::CLIMATE_SWING_VERTICAL:
            dataTX[10] += 0b00111000; break;
        case climate::CLIMATE_SWING_HORIZONTAL:
            dataTX[11] += 0b00001000; break;
        case climate::CLIMATE_SWING_BOTH:
            dataTX[10] += 0b00111000; dataTX[11] += 0b00001000; break;
    }

    // Устанавливаем предустановки кондиционера
    // Set AC presets
    if (this->preset.has_value()) {
        switch (*this->preset) {
            case ClimatePreset::CLIMATE_PRESET_NONE:    break;
            case ClimatePreset::CLIMATE_PRESET_ECO:     dataTX[7]  += 0b10000000; break;
            case ClimatePreset::CLIMATE_PRESET_SLEEP:   dataTX[19] += 0b00000001; break;
            case ClimatePreset::CLIMATE_PRESET_COMFORT: dataTX[8]  += 0b00010000; break;
        }
    }

    //Режим заслонок
    //  Вертикальная заслонка
    //      Качание вертикальной заслонки [10 байт, маска 00111000]:
    //          000 - Качание отключено, заслонка в последней позиции или в фиксации
    //          111 - Качание включено в выбранном режиме
    //      Режим качания вертикальной заслонки (режим фиксации заслонки роли не играет, если качание включено) [32 байт, маска 00011000]:
    //          01 - качание сверху вниз, ПО УМОЛЧАНИЮ
    //          10 - качание в верхней половине
    //          11 - качание в нижней половине
    //      Режим фиксации заслонки (режим качания заслонки роли не играет, если качание выключено) [32 байт, маска 00000111]:
    //          000 - нет фиксации, ПО УМОЛЧАНИЮ
    //          001 - фиксация вверху
    //          010 - фиксация между верхом и серединой
    //          011 - фиксация в середине
    //          100 - фиксация между серединой и низом
    //          101 - фиксация внизу
    //  Горизонтальные заслонки
    //      Качание горизонтальных заслонок [11 байт, маска 00001000]:
    //          0 - Качание отключено, заслонки в последней позиции или в фиксации
    //          1 - Качание включено в выбранном режиме
    //      Режим качания горизонтальных заслонок (режим фиксации заслонок роли не играет, если качание включено) [33 байт, маска 00111000]:
    //          001 - качание слева направо, ПО УМОЛЧАНИЮ
    //          010 - качание слева
    //          011 - качание по середине
    //          100 - качание справа
    //      Режим фиксации горизонтальных заслонок (режим качания заслонок роли не играет, если качание выключено) [33 байт, маска 00000111]:
    //          000 - нет фиксации, ПО УМОЛЧАНИЮ
    //          001 - фиксация слева
    //          010 - фиксация между левой стороной и серединой
    //          011 - фиксация в середине
    //          100 - фиксация между серединой и правой стороной
    //          101 - фиксация справа
    //
    // Vane mode
    //   Vertical vane
    //     Vertical vane swing [byte 10, mask 00111000]:
    //       000 - swing off, vane in last position or fixed
    //       111 - swing on in the selected mode
    //     Vertical vane swing mode (fix mode has no effect when swing is on) [byte 32, mask 00011000]:
    //       01 - swing top to bottom, DEFAULT
    //       10 - swing in upper half
    //       11 - swing in lower half
    //     Vane fix position (swing mode has no effect when swing is off) [byte 32, mask 00000111]:
    //       000 - no fix, DEFAULT
    //       001 - fixed at top
    //       010 - fixed between top and center
    //       011 - fixed at center
    //       100 - fixed between center and bottom
    //       101 - fixed at bottom
    //   Horizontal vanes
    //     Horizontal vane swing [byte 11, mask 00001000]:
    //       0 - swing off, vanes in last position or fixed
    //       1 - swing on in the selected mode
    //     Horizontal vane swing mode (fix mode has no effect when swing is on) [byte 33, mask 00111000]:
    //       001 - swing left to right, DEFAULT
    //       010 - swing left
    //       011 - swing center
    //       100 - swing right
    //     Horizontal vane fix position (swing mode has no effect when swing is off) [byte 33, mask 00000111]:
    //       000 - no fix, DEFAULT
    //       001 - fixed left
    //       010 - fixed between left and center
    //       011 - fixed at center
    //       100 - fixed between center and right
    //       101 - fixed right

    // Устанавливаем режим для качания вертикальной заслонки
    // Set vertical vane swing mode
    // Vertical vane swing range
    switch (vertical_swing_direction_) {
        case VerticalSwingDirection::UP_DOWN:  dataTX[32] += 0b00001000; break;
        case VerticalSwingDirection::UPSIDE:   dataTX[32] += 0b00010000; break;
        case VerticalSwingDirection::DOWNSIDE: dataTX[32] += 0b00011000; break;
    }
    // Устанавливаем режим для качания горизонтальных заслонок
    // Set horizontal vane swing mode
    // Horizontal vane swing range
    switch (horizontal_swing_direction_) {
        case HorizontalSwingDirection::LEFT_RIGHT: dataTX[33] += 0b00001000; break;
        case HorizontalSwingDirection::LEFTSIDE:   dataTX[33] += 0b00010000; break;
        case HorizontalSwingDirection::CENTER:     dataTX[33] += 0b00011000; break;
        case HorizontalSwingDirection::RIGHTSIDE:  dataTX[33] += 0b00100000; break;
    }
    // Устанавливаем положение фиксации вертикальной заслонки
    // Set vertical vane fixed position
    // Vertical fixed position
    switch (vertical_direction_) {
        case AirflowVerticalDirection::LAST:     break;
        case AirflowVerticalDirection::MAX_UP:   dataTX[32] += 0b00000001; break;
        case AirflowVerticalDirection::UP:       dataTX[32] += 0b00000010; break;
        case AirflowVerticalDirection::CENTER:   dataTX[32] += 0b00000011; break;
        case AirflowVerticalDirection::DOWN:     dataTX[32] += 0b00000100; break;
        case AirflowVerticalDirection::MAX_DOWN: dataTX[32] += 0b00000101; break;
    }
    // Устанавливаем положение фиксации горизонтальных заслонок
    // Set horizontal vane fixed position
    // Horizontal fixed position
    switch (horizontal_direction_) {
        case AirflowHorizontalDirection::LAST:      break;
        case AirflowHorizontalDirection::MAX_LEFT:  dataTX[33] += 0b00000001; break;
        case AirflowHorizontalDirection::LEFT:      dataTX[33] += 0b00000010; break;
        case AirflowHorizontalDirection::CENTER:    dataTX[33] += 0b00000011; break;
        case AirflowHorizontalDirection::RIGHT:     dataTX[33] += 0b00000100; break;
        case AirflowHorizontalDirection::MAX_RIGHT: dataTX[33] += 0b00000101; break;
    }

    // Установка температуры
    // Set target temperature
    dataTX[9] = target_temperature_set;

    // Собираем массив байт для отправки в кондиционер
    // Assemble byte array to send to the AC
    dataTX[0]  = 0xBB; //стартовый байт заголовка — frame header start byte
    dataTX[1]  = 0x00; //стартовый байт заголовка — frame header start byte
    dataTX[2]  = 0x01; //стартовый байт заголовка — frame header start byte
    dataTX[3]  = 0x03; //0x03 - управление, 0x04 - опрос — 0x03 = control, 0x04 = poll
    dataTX[4]  = 0x20; //0x20 - управление, 0x19 - опрос — 0x20 = control, 0x19 = poll
    dataTX[5]  = 0x03; //??
    dataTX[6]  = 0x01; //??
    //dataTX[7]  eco,display,beep,on-timer-enable,off-timer-enable,power,0,0
    //dataTX[8]  mute,0,turbo,health, mode(4): 01=heat 02=dry 03=cool 07=fan 08=auto
    //dataTX[9]  0–31; settemp = 31 – x, 0,0,0,0,temp(4)
    //dataTX[10] 0,timer-indicator,swingv(3),fan(3) fan+swing modes: 0=auto 1=low 2=med 3=high
    //dataTX[11] 0,off-timer(6),0
    dataTX[12] = 0x00; //fahrenheit,on-timer(6),0  cf: 0x80=°F 0x00=°C
    dataTX[13] = 0x01; //??
    dataTX[14] = 0x00; //0,0,half-degree,0,0,0,0,0
    dataTX[15] = 0x00; //??
    dataTX[16] = 0x00; //??
    dataTX[17] = 0x00; //??
    dataTX[18] = 0x00; //??
    //dataTX[19] sleep: 1=on 0=off
    dataTX[20] = 0x00; //??
    dataTX[21] = 0x00; //??
    dataTX[22] = 0x00; //??
    dataTX[23] = 0x00; //??
    dataTX[24] = 0x00; //??
    dataTX[25] = 0x00; //??
    dataTX[26] = 0x00; //??
    dataTX[27] = 0x00; //??
    dataTX[28] = 0x00; //??
    dataTX[30] = 0x00; //??
    dataTX[31] = 0x00; //??
    //dataTX[32] 0,0,0,режим вертикального качания(2),режим вертикальной фиксации(3)
    //           0,0,0,vertical swing mode(2),vertical fix mode(3)
    //dataTX[33] 0,0,режим горизонтального качания(3),режим горизонтальной фиксации(3)
    //           0,0,horizontal swing mode(3),horizontal fix mode(3)
    dataTX[34] = 0x00; //??
    dataTX[35] = 0x00; //??
    dataTX[36] = 0x00; //??
    dataTX[37] = 0xFF; //Контрольная сумма — checksum placeholder
    dataTX[37] = getChecksum(dataTX, sizeof(dataTX));

    sendData(dataTX, sizeof(dataTX));
    allow_take_control = false;
}

// Отправка данных в кондиционер
// Send data to the air conditioner
void tclacClimate::sendData(uint8_t *message, uint8_t size) {
    dataShow(1, 1);
    this->esphome::uart::UARTDevice::write_array(message, size);
    ESP_LOGD("TCL_EXT", "TX sent");
    dataShow(1, 0);
}

// Преобразование байта в читабельный формат
// Convert byte array to readable hex format
String tclacClimate::getHex(uint8_t *message, uint8_t size) {
    String raw;
    for (int i = 0; i < size; i++)
        raw += "\n" + String(message[i]);
    raw.toUpperCase();
    return raw;
}

// Вычисление контрольной суммы
// Calculate checksum (XOR of all bytes except the last)
uint8_t tclacClimate::getChecksum(const uint8_t *message, size_t size) {
    uint8_t crc = 0;
    for (size_t i = 0; i < size - 1; i++)
        crc ^= message[i];
    return crc;
}

// Мигаем светодиодами
// Toggle indicator LEDs
void tclacClimate::dataShow(bool flow, bool shine) {
    if (!module_display_status_) return;
    if (!flow) {
#ifdef CONF_RX_LED
        this->rx_led_pin_->digital_write(shine);
#endif
    } else {
#ifdef CONF_TX_LED
        this->tx_led_pin_->digital_write(shine);
#endif
    }
}

// Действия с данными из конфига
// Actions with data from config

// Получение состояния пищалки
// Set beeper state
void tclacClimate::set_beeper_state(bool state) {
    beeper_status_ = state;
    if (force_mode_status_ && allow_take_control) takeControl();
}
// Получение состояния дисплея кондиционера
// Set AC display state
void tclacClimate::set_display_state(bool disp_state) {
    display_status_ = disp_state;
    if (force_mode_status_ && allow_take_control) takeControl();
}
// Получение состояния режима принудительного применения настроек
// Set force-settings mode state
void tclacClimate::set_force_mode_state(bool f_state)  { force_mode_status_    = f_state; }
// Получение состояния светодиодов связи модуля
// Set module indicator LED state
void tclacClimate::set_module_display_state(bool d_state) { module_display_status_ = d_state; }

// Получение пина светодиода приема данных
// Set RX LED pin
#ifdef CONF_RX_LED
void tclacClimate::set_rx_led_pin(GPIOPin *pin) { rx_led_pin_ = pin; }
#endif
// Получение пина светодиода передачи данных
// Set TX LED pin
#ifdef CONF_TX_LED
void tclacClimate::set_tx_led_pin(GPIOPin *pin) { tx_led_pin_ = pin; }
#endif

// Получение режима фиксации вертикальной заслонки
// Set vertical vane fix position
void tclacClimate::set_vertical_airflow(AirflowVerticalDirection v_airflow) {
    vertical_direction_ = v_airflow;
    if (force_mode_status_ && allow_take_control) takeControl();
}
// Получение режима фиксации горизонтальных заслонок
// Set horizontal vane fix position
void tclacClimate::set_horizontal_airflow(AirflowHorizontalDirection h_airflow) {
    horizontal_direction_ = h_airflow;
    if (force_mode_status_ && allow_take_control) takeControl();
}
// Получение режима качания вертикальной заслонки
// Set vertical vane swing mode
void tclacClimate::set_vertical_swing_direction(VerticalSwingDirection vs_direction) {
    vertical_swing_direction_ = vs_direction;
    if (force_mode_status_ && allow_take_control) takeControl();
}
// Получение режима качания горизонтальных заслонок
// Set horizontal vane swing mode
void tclacClimate::set_horizontal_swing_direction(HorizontalSwingDirection hs_direction) {
    horizontal_swing_direction_ = hs_direction;
    if (force_mode_status_ && allow_take_control) takeControl();
}

// Получение доступных режимов работы кондиционера
// Set supported AC operating modes
void tclacClimate::set_supported_modes(climate::ClimateModeMask m)      { supported_modes_      = m; }
// Получение доступных предустановок
// Set supported presets
void tclacClimate::set_supported_presets(climate::ClimatePresetMask m)   { supported_presets_    = m; }
// Получение доступных скоростей вентилятора
// Set supported fan modes
void tclacClimate::set_supported_fan_modes(climate::ClimateFanModeMask m){ supported_fan_modes_  = m; }
// Получение доступных режимов качания заслонок
// Set supported swing modes
void tclacClimate::set_supported_swing_modes(climate::ClimateSwingModeMask m) { supported_swing_modes_ = m; }

} // namespace tclac
} // namespace esphome
