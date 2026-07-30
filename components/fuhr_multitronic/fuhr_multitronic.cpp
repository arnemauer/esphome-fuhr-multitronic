#include "fuhr_multitronic.h"
#include "esphome/core/log.h"

namespace esphome {
namespace fuhr_multitronic {

static const char *const TAG = "fuhr_multitronic";

void IRAM_ATTR FuhrMultitronic::gpio_intr(FuhrMultitronic *self) {
  uint8_t next = (uint8_t) ((self->head_ + 1) % EDGE_BUF);
  if (next == self->tail_) {
    self->isr_overflow_++;
    return;
  }
  self->edges_[self->head_].time = micros();
  self->edges_[self->head_].bus = self->bus_isr_.digital_read();
  self->edges_[self->head_].ctrl = self->has_ctrl_ ? self->ctrl_isr_.digital_read() : false;
  self->head_ = next;
}

void FuhrMultitronic::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Fuhr multitronic bus...");
  this->bus_pin_->setup();
  this->bus_isr_ = this->bus_pin_->to_isr();
  if (this->ctrl_pin_ != nullptr) {
    this->ctrl_pin_->setup();
    this->ctrl_isr_ = this->ctrl_pin_->to_isr();
    this->has_ctrl_ = true;
  }
  this->decoder_.set_bit_time_us(1000000UL / this->baud_rate_);
  this->decoder_.reset();
  this->state_.reset();
  this->bus_pin_->attach_interrupt(&FuhrMultitronic::gpio_intr, this,
                                   gpio::INTERRUPT_ANY_EDGE);
  this->publish_states_();
}

void FuhrMultitronic::loop() {
  // Drain the ISR ring buffer.
  while (this->tail_ != this->head_) {
    IsrEdge e;
    e.time = this->edges_[this->tail_].time;
    e.bus = this->edges_[this->tail_].bus;
    e.ctrl = this->edges_[this->tail_].ctrl;
    this->tail_ = (uint8_t) ((this->tail_ + 1) % EDGE_BUF);
    this->decoder_.on_edge(e.time, e.bus, e.ctrl, this->has_ctrl_);
  }
  this->decoder_.poll(micros());

  fuhr::DecodedFrame f;
  while (this->decoder_.pop(&f))
    this->handle_frame_(f);

  if (this->isr_overflow_ != 0) {
    uint32_t n = this->isr_overflow_;
    this->isr_overflow_ = 0;
    ESP_LOGW(TAG, "Missed %u edges (ISR buffer full)", n);
  }
  if (this->decoder_.dropped() != 0)
    ESP_LOGW(TAG, "Dropped %u frames (queue full)", this->decoder_.dropped());

  // The lock repeats its status every ~15 s; the controller itself falls back
  // to a fault after 21 s of silence.
  bool conn = this->ever_received_ &&
              (millis() - this->last_rx_ms_) < this->timeout_ms_;
  if (conn != this->connected_ || !this->connected_valid_) {
    this->connected_ = conn;
    this->connected_valid_ = true;
#ifdef USE_BINARY_SENSOR
    if (this->connected_sensor_ != nullptr)
      this->connected_sensor_->publish_state(conn);
#endif
    if (!conn && this->ever_received_)
      ESP_LOGW(TAG, "No message for %u ms - lost contact with the lock",
               this->timeout_ms_);
  }
}

void FuhrMultitronic::handle_frame_(const fuhr::DecodedFrame &f) {
  if (f.is_ack) {
    ESP_LOGV(TAG, "ACK from %s",
             f.sender == fuhr::SENDER_CTRL ? "controller" : "lock");
    if (!this->publish_acks_)
      return;
#ifdef USE_TEXT_SENSOR
    if (this->message_sensor_ != nullptr)
      this->message_sensor_->publish_state(
          f.sender == fuhr::SENDER_CTRL ? "CTRL ACK" : "LOCK ACK");
#endif
    return;
  }

  this->last_rx_ms_ = millis();
  this->ever_received_ = true;

  if (!f.manchester_ok || !f.crc_ok) {
    this->frames_bad_++;
    ESP_LOGW(TAG, "Bad frame: line=%08X code=0x%02X crc=0x%02X (%s)",
             f.line_bits, f.code, f.crc,
             !f.manchester_ok ? "manchester" : "crc");
#ifdef USE_TEXT_SENSOR
    if (this->message_sensor_ != nullptr) {
      char buf[64];
      snprintf(buf, sizeof(buf), "ERROR 0x%02X (%s)", f.code,
               !f.manchester_ok ? "manchester" : "crc");
      this->message_sensor_->publish_state(buf);
    }
#endif
    return;
  }

  this->frames_ok_++;
  bool from_ctrl = (f.sender == fuhr::SENDER_CTRL);
  const char *who = from_ctrl ? "CTRL" : "LOCK";
  const char *name = fuhr::message_name(f.code);

  char buf[96];
  snprintf(buf, sizeof(buf), "%s 0x%02X %s", who, f.code, name);
  ESP_LOGD(TAG, "%s", buf);

#ifdef USE_TEXT_SENSOR
  if (this->message_sensor_ != nullptr)
    this->message_sensor_->publish_state(buf);
#endif

  this->state_.apply(f.code);
  this->publish_states_();
  this->message_callback_.call(f.code, std::string(name), from_ctrl);
}

void FuhrMultitronic::publish_states_() {
#ifdef USE_TEXT_SENSOR
  bool first = !this->published_once_;
  if (this->door_sensor_ != nullptr && (first || this->state_.door != this->last_door_))
    this->door_sensor_->publish_state(fuhr::StateTracker::door_str(this->state_.door));
  if (this->lock_sensor_ != nullptr && (first || this->state_.lock != this->last_lock_))
    this->lock_sensor_->publish_state(fuhr::StateTracker::lock_str(this->state_.lock));
  if (this->day_mode_sensor_ != nullptr && (first || this->state_.day != this->last_day_))
    this->day_mode_sensor_->publish_state(fuhr::StateTracker::day_str(this->state_.day));
  if (this->error_sensor_ != nullptr && (first || this->state_.error != this->last_error_))
    this->error_sensor_->publish_state(fuhr::StateTracker::error_str(this->state_.error));
  this->published_once_ = true;
#endif
  this->last_door_ = this->state_.door;
  this->last_lock_ = this->state_.lock;
  this->last_day_ = this->state_.day;
  this->last_error_ = this->state_.error;
}

void FuhrMultitronic::dump_config() {
  ESP_LOGCONFIG(TAG, "Fuhr multitronic bus:");
  LOG_PIN("  Bus pin: ", this->bus_pin_);
  if (this->ctrl_pin_ != nullptr) {
    LOG_PIN("  Controller TX pin: ", this->ctrl_pin_);
  } else {
    ESP_LOGCONFIG(TAG, "  Controller TX pin: not used "
                       "(direction is derived from the message code)");
  }
  ESP_LOGCONFIG(TAG, "  Bit rate: %u baud (%u us/bit)", this->baud_rate_,
                this->decoder_.bit_time_us());
  ESP_LOGCONFIG(TAG, "  Timeout: %u ms", this->timeout_ms_);
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Message", this->message_sensor_);
  LOG_TEXT_SENSOR("  ", "Door", this->door_sensor_);
  LOG_TEXT_SENSOR("  ", "Lock", this->lock_sensor_);
  LOG_TEXT_SENSOR("  ", "Day mode", this->day_mode_sensor_);
  LOG_TEXT_SENSOR("  ", "Error", this->error_sensor_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_sensor_);
#endif
}

}  // namespace fuhr_multitronic
}  // namespace esphome
