#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化并启动蓝牙配网服务
 * 该函数会生成随机 PIN 码并启动广播
 */
void start_new_network_provisioning(void);

/**
 * @brief 获取当前生成的 PIN 码
 * @return 返回 6 位数字符串 (例如 "123456")
 */
const char* get_ble_pin(void);

/**
 * @brief 检查设备是否已经完成过配网
 * @return true 已配网, false 未配网
 */
bool is_device_provisioned(void);

/**
 * @brief 清除設備的配網狀態與 Wi-Fi 密碼
 */
void reset_device_provisioning(void);

#ifdef __cplusplus
}
#endif