#include "buzzer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 定义蜂鸣器引脚
#ifndef BUZZER_PIN
#define BUZZER_PIN 2
#endif

// ⚠️ 注意：不要和 LCD 背光的 Timer/Channel 冲突！
// 假设背光用了 TIMER_0 和 CHANNEL_0，这里我们用 TIMER_1 和 CHANNEL_1
#define BUZZER_LEDC_TIMER   LEDC_TIMER_2
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_2

void buzzer_init(void) {
    // 1. 配置 LEDC 定时器
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_10_BIT, // 10位分辨率 (0-1023)
        .timer_num        = BUZZER_LEDC_TIMER,
        .freq_hz          = 2700,              // MLT-7525 最佳频率
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // 2. 配置 LEDC 通道
    ledc_channel_config_t channel_conf = {
        .gpio_num       = BUZZER_PIN,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = BUZZER_LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = BUZZER_LEDC_TIMER,
        .duty           = 0,                   // 初始占空比为 0 (不响)
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    gpio_set_drive_capability(BUZZER_PIN, GPIO_DRIVE_CAP_3);
}

// 发声函数：参数为 频率(Hz) 和 持续时间(毫秒)
void buzzer_beep(uint32_t freq, uint32_t duration_ms) {
    if (freq > 0) {
        // 设置频率
        ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_TIMER, freq);
        // 设置占空比为 50% (10位分辨率的一半：512)，声音最大
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, 512);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);
    }
    
    // 阻塞延时
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    // 关闭声音
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);
}