#include "speed.h"
#include <board/board.h>
#include <io.h>
#include "src/HAL/hal_pwm.h"
#include <wirish_time.h>
#include <ext_interrupts.h>
#include "src/HAL/hal_tim.h"
#include "src/core/can_bus.h"
#include "src/registry/registry.h"
#include "src/configuration.h"
uint32_t speed_exti_count = 0; 
uint32_t speed_tim_count = 0;
uint8_t motor_Lap_pulse = 0;

void Speed::GetMotorLapPulse() {
    ModuleMacInfo * parm = (ModuleMacInfo *)(FLASH_MODULE_PARA);
    if (parm->other_parm[0] > 0 && parm->other_parm[0] <=4) {
      motor_Lap_pulse = parm->other_parm[0];
    } else {
      motor_Lap_pulse = DEFAULT_MOTOR_LAP_PULSE;
    }
}


void Speed::InitOut(uint8_t pwm_pin, uint8_t tim_num, uint8_t tim_chn) {
  this->pwm_tim_chn_ = tim_chn;
  this->pwm_tim_num_ = tim_num;
  pinMode(pwm_pin, PWM);
  HAL_pwn_config(tim_num, tim_chn, 2000000, MAX_SPEED_OUT, 0, 0);
}
void Speed::InitDir(uint8_t dir_pin, uint8_t dir) {
  pinMode(dir_pin, OUTPUT);
  digitalWrite(dir_pin, dir);
}

void FgExtiCallBack() {
  speed_exti_count++;
}

void FgTimCallBack() {
  speed_tim_count = speed_exti_count;
  speed_exti_count = 0;
}

void Speed::InitCapture(uint8_t fg_pin, uint8_t tim_num) {
  attachInterrupt(fg_pin, FgExtiCallBack, FALLING);
  HAL_timer_init(tim_num, 7200, 10000 / SPEED_CAPTURE_FREQUENCY);
  HAL_timer_nvic_init(tim_num, 1, 1);
  HAL_timer_cb_init(tim_num, FgTimCallBack);
  HAL_timer_enable(tim_num);
  GetMotorLapPulse();
}

uint16_t Speed::ReadCurSpeed() {
  return (speed_tim_count * SPEED_TO_RPM_RATE);
}

void Speed::SetSpeed(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  this->target_speed_ = percent;
  this->speed_fail_flag_ = false;
  this->set_speed_time_ =  millis();
}

void Speed::ReportSpeed() {
  uint16_t msgid = registryInstance.FuncId2MsgId(FUNC_REPORT_MOTOR_SPEED);

  if (msgid != INVALID_VALUE) {
    uint16_t cur_speed = this->ReadCurSpeed();
    uint8_t data[8];
    uint8_t index = 0;
    data[index++] = cur_speed >> 8;
    data[index++] = cur_speed;
    if (this->SpeedStatuCheck() == true) {
      data[index++] = 0;  // normal
    } else {
      data[index++] = 1;  // fail
    }
    canbus_g.PushSendStandardData(msgid, data, index);
  }
}


bool Speed::SpeedStatuCheck() {
  uint32_t normal_speed = CNC_MAX_RPM * this->target_speed_ / 100;
  uint32_t min_speed = normal_speed * 60 / 100;

  if ((this->target_speed_ > 0) && this->ReadCurSpeed() < min_speed) {
    if (this->speed_fail_flag_ == false) {
      this->speed_fail_flag_ = true;
      this->set_speed_time_ =  millis();
    }
    if (this->speed_fail_flag_ == true) {
      if ((this->set_speed_time_ + 3000) < millis()) {
        HAL_pwm_set_pulse(this->pwm_tim_num_, this->pwm_tim_chn_, 0);
        return false;
      }
    }
  } else {
    this->speed_fail_flag_ = false;
  }
  return true;
}

// need loop
void Speed::SpeedOutCtrl() {
  uint16_t out_pwm = 0;
  if ((this->change_time_ + 10) < millis()) {
    this->change_time_ = millis();
    if (this->target_speed_ != this->cur_set_percent_) {
      if (this->target_speed_ > this->cur_set_percent_) {
        this->cur_set_percent_ = this->target_speed_;
      } else if (this->target_speed_ < this->cur_set_percent_) {
        this->cur_set_percent_--;
      }
      out_pwm = (this->cur_set_percent_ * MAX_SPEED_OUT) / 100;
      HAL_pwm_set_pulse(this->pwm_tim_num_, this->pwm_tim_chn_, out_pwm);
    }
  }
}