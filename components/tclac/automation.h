#pragma once
#include "tclac.h"
#include "esphome/core/automation.h"

namespace esphome {
namespace tclac {

// Шаблон действия: включение пищалки
// Action template: turn beeper on
template<typename... Ts> class BeeperOnAction : public Action<Ts...> {
public:
    explicit BeeperOnAction(tclacClimate *p) : p_(p) {}
    void play(Ts... x) override { p_->set_beeper_state(true); }
private: tclacClimate *p_;
};
// Шаблон действия: выклюение пищалки
// Action template: turn beeper off
template<typename... Ts> class BeeperOffAction : public Action<Ts...> {
public:
    explicit BeeperOffAction(tclacClimate *p) : p_(p) {}
    void play(Ts... x) override { p_->set_beeper_state(false); }
private: tclacClimate *p_;
};

// Шаблон действия: включение дисплея
// Action template: turn display on
template<typename... Ts> class DisplayOnAction : public Action<Ts...> {
public:
    explicit DisplayOnAction(tclacClimate *p) : p_(p) {}
    void play(Ts... x) override { p_->set_display_state(true); }
private: tclacClimate *p_;
};
// Шаблон действия: выключение дисплея
// Action template: turn display off
template<typename... Ts> class DisplayOffAction : public Action<Ts...> {
public:
    explicit DisplayOffAction(tclacClimate *p) : p_(p) {}
    void play(Ts... x) override { p_->set_display_state(false); }
private: tclacClimate *p_;
};

// Шаблон действия: включение принудительного применения настроек
// Action template: enable forced settings mode
template<typename... Ts> class ForceOnAction : public Action<Ts...> {
public:
    explicit ForceOnAction(tclacClimate *p) : p_(p) {}
    void play(Ts... x) override { p_->set_force_mode_state(true); }
private: tclacClimate *p_;
};
// Шаблон действия: выключение принудительного применения настроек
// Action template: disable forced settings mode
template<typename... Ts> class ForceOffAction : public Action<Ts...> {
public:
    explicit ForceOffAction(tclacClimate *p) : p_(p) {}
    void play(Ts... x) override { p_->set_force_mode_state(false); }
private: tclacClimate *p_;
};

// Шаблон действия: включение индикатора модуля
// Action template: turn module indicator on
template<typename... Ts> class ModuleDisplayOnAction : public Action<Ts...> {
public:
    explicit ModuleDisplayOnAction(tclacClimate *p) : p_(p) {}
    void play(Ts... x) override { p_->set_module_display_state(true); }
private: tclacClimate *p_;
};
// Шаблон действия: выключение индикатора модуля
// Action template: turn module indicator off
template<typename... Ts> class ModuleDisplayOffAction : public Action<Ts...> {
public:
    explicit ModuleDisplayOffAction(tclacClimate *p) : p_(p) {}
    void play(Ts... x) override { p_->set_module_display_state(false); }
private: tclacClimate *p_;
};

// Шаблон действия: изменение вертикальной фиксации заслонки
// Action template: change vertical vane fix position
template<typename... Ts> class VerticalAirflowAction : public Action<Ts...> {
public:
    explicit VerticalAirflowAction(tclacClimate *p) : p_(p) {}
    TEMPLATABLE_VALUE(AirflowVerticalDirection, direction)
    void play(Ts... x) override { p_->set_vertical_airflow(direction_.value(x...)); }
private: tclacClimate *p_;
};

// Шаблон действия: изменение горизонтальной фиксации заслонок
// Action template: change horizontal vane fix position
template<typename... Ts> class HorizontalAirflowAction : public Action<Ts...> {
public:
    explicit HorizontalAirflowAction(tclacClimate *p) : p_(p) {}
    TEMPLATABLE_VALUE(AirflowHorizontalDirection, direction)
    void play(Ts... x) override { p_->set_horizontal_airflow(direction_.value(x...)); }
private: tclacClimate *p_;
};

// Шаблон действия: изменение режима вертикального качания заслонки
// Action template: change vertical vane swing mode
template<typename... Ts> class VerticalSwingDirectionAction : public Action<Ts...> {
public:
    explicit VerticalSwingDirectionAction(tclacClimate *p) : p_(p) {}
    TEMPLATABLE_VALUE(VerticalSwingDirection, swing_direction)
    void play(Ts... x) override { p_->set_vertical_swing_direction(swing_direction_.value(x...)); }
private: tclacClimate *p_;
};

// Шаблон действия: изменение режима горизонтального качания заслонок
// Action template: change horizontal vane swing mode
template<typename... Ts> class HorizontalSwingDirectionAction : public Action<Ts...> {
public:
    explicit HorizontalSwingDirectionAction(tclacClimate *p) : p_(p) {}
    TEMPLATABLE_VALUE(HorizontalSwingDirection, swing_direction)
    void play(Ts... x) override { p_->set_horizontal_swing_direction(swing_direction_.value(x...)); }
private: tclacClimate *p_;
};

} // namespace tclac
} // namespace esphome
