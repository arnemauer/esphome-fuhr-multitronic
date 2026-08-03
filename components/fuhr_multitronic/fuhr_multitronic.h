#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/automation.h"
#include "fuhr_protocol.h"

#include <cstdio>
#include <functional>

#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include <string>

namespace esphome {
namespace fuhr_multitronic {

static const uint8_t EDGE_BUF = 64;

struct IsrEdge {
  uint32_t time;
  bool bus;
  bool ctrl;
};

class FuhrMultitronic : public Component {
 public:
  void set_bus_pin(InternalGPIOPin *pin) { this->bus_pin_ = pin; }
  void set_ctrl_pin(InternalGPIOPin *pin) { this->ctrl_pin_ = pin; }
  void set_baud_rate(uint32_t baud) { this->baud_rate_ = baud; }
  void set_timeout(uint32_t ms) { this->timeout_ms_ = ms; }
  void set_publish_acks(bool v) { this->publish_acks_ = v; }

#ifdef USE_TEXT_SENSOR
  void set_message_sensor(text_sensor::TextSensor *s) { this->message_sensor_ = s; }
  void set_door_sensor(text_sensor::TextSensor *s) { this->door_sensor_ = s; }
  void set_lock_sensor(text_sensor::TextSensor *s) { this->lock_sensor_ = s; }
  void set_day_mode_sensor(text_sensor::TextSensor *s) { this->day_mode_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_fault_sensor(binary_sensor::BinarySensor *s) { this->fault_sensor_ = s; }
  void set_tamper_sensor(binary_sensor::BinarySensor *s) { this->tamper_sensor_ = s; }
#endif

  void add_message_callback(std::function<void(uint8_t, std::string, bool)> &&cb) {
    this->message_callback_.add(std::move(cb));
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Counters, handy for diagnostics.
  uint32_t frames_ok() const { return this->frames_ok_; }
  uint32_t frames_bad() const { return this->frames_bad_; }

 protected:
  static void gpio_intr(FuhrMultitronic *self);
  void handle_frame_(const fuhr::DecodedFrame &f);
  void publish_states_();

  InternalGPIOPin *bus_pin_{nullptr};
  InternalGPIOPin *ctrl_pin_{nullptr};
  ISRInternalGPIOPin bus_isr_;
  ISRInternalGPIOPin ctrl_isr_;
  bool has_ctrl_{false};

  uint32_t baud_rate_{2000};
  uint32_t timeout_ms_{30000};
  bool publish_acks_{false};

  // Written by the ISR, read by loop().
  volatile IsrEdge edges_[EDGE_BUF];
  volatile uint8_t head_{0};
  volatile uint8_t tail_{0};
  volatile uint32_t isr_overflow_{0};

  fuhr::FrameDecoder decoder_;
  fuhr::StateTracker state_;

  uint32_t frames_ok_{0};
  uint32_t frames_bad_{0};
  uint32_t last_rx_ms_{0};
  bool ever_received_{false};
  bool connected_{false};
  bool connected_valid_{false};

  fuhr::DoorState last_door_{fuhr::DOOR_UNKNOWN};
  fuhr::LockState last_lock_{fuhr::LOCK_UNKNOWN};
  fuhr::DayMode last_day_{fuhr::DAY_UNKNOWN};
  fuhr::Tri last_fault_{fuhr::TRI_UNKNOWN};
  fuhr::Tri last_tamper_{fuhr::TRI_UNKNOWN};
  bool published_once_{false};

  CallbackManager<void(uint8_t, std::string, bool)> message_callback_;

#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *message_sensor_{nullptr};
  text_sensor::TextSensor *door_sensor_{nullptr};
  text_sensor::TextSensor *lock_sensor_{nullptr};
  text_sensor::TextSensor *day_mode_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  binary_sensor::BinarySensor *fault_sensor_{nullptr};
  binary_sensor::BinarySensor *tamper_sensor_{nullptr};
#endif
};

class MessageTrigger : public Trigger<uint8_t, std::string, bool> {
 public:
  explicit MessageTrigger(FuhrMultitronic *parent) {
    parent->add_message_callback([this](uint8_t code, std::string name, bool from_ctrl) {
      this->trigger(code, name, from_ctrl);
    });
  }
};

}  // namespace fuhr_multitronic
}  // namespace esphome
