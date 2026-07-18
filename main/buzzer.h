#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

/**
 * @brief 初始化蜂鸣器 PWM (LEDC) 控制器
 * @note 请在 app_main() 的初期调用，且需注意与背光 PWM 通道不要冲突
 */
void buzzer_init(void);

/**
 * @brief 触发蜂鸣器发声 (阻塞型)
 * * @param freq        声音频率 (Hz)，例如 MLT-7525 推荐 2700
 * @param duration_ms 发声持续时间 (毫秒)
 */
void buzzer_beep(uint32_t freq, uint32_t duration_ms);

#endif // BUZZER_H