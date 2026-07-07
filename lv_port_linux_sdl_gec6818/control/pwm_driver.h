/**
 * ============================================================
 *  PWM 电机驱动模块 — GEC6818 Linux sysfs 接口
 * ============================================================
 *
 *  GEC6818 (S5P6818) 具有 5 路 PWM 定时器：
 *   - PWM0 (TOUT0): pwmchip0/pwm0  → 增氧机
 *   - PWM1 (TOUT1): pwmchip0/pwm1  → 投喂电机
 *   - PWM2 (TOUT2): pwmchip0/pwm2  → 循环水泵
 *
 *  Linux sysfs 操作路径：
 *   /sys/class/pwm/pwmchip0/
 *     export    → 写入通道号以启用
 *     pwmX/
 *       period     → PWM 周期（纳秒）
 *       duty_cycle → 占空比（纳秒）
 *       enable     → 使能（1=开, 0=关）
 *
 *  注意：S5P6818 的 PWM 可能挂载为 pwmchip1 或 pwmchip2，
 *       请根据实际板子 `/sys/class/pwm/` 下的目录调整 PWM_CHIP。
 */

#ifndef __PWM_DRIVER_H
#define __PWM_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ========== PWM 通道 → 设备映射 ========== */
#define PWM_AERATOR_CHANNEL     0    /* 增氧机   → PWM0 */
#define PWM_FEEDER_CHANNEL      1    /* 投喂电机 → PWM1 */
#define PWM_WATERPUMP_CHANNEL   2    /* 循环水泵 → PWM2 */

/* ========== PWM 频率/周期配置 ========== */
#define PWM_FREQ_HZ             1000    /* PWM 频率 1kHz（电机控制常用） */
#define PWM_PERIOD_NS           1000000 /* 周期 = 1/1000 Hz = 1,000,000 ns */

/* ========== 函数声明 ========== */

/**
 * @brief 初始化所有 PWM 通道
 * @return 成功初始化的通道数
 *
 * 操作：export → 设置 period → enable
 */
int PWM_InitAll(void);

/**
 * @brief 设置指定通道的占空比
 * @param channel     PWM 通道号 (0~4)
 * @param duty_percent 占空比 0.0 ~ 100.0
 * @return 0=成功, -1=失败
 */
int PWM_SetDuty(int channel, float duty_percent);

/**
 * @brief 开启/关闭指定 PWM 通道
 * @param channel PWM 通道号
 * @param on      1=开启, 0=关闭
 * @return 0=成功, -1=失败
 */
int PWM_Enable(int channel, int on);

/**
 * @brief 获取当前占空比（读 sysfs）
 * @param channel PWM 通道号
 * @return 占空比 0.0~100.0，-1 表示失败
 */
float PWM_GetDuty(int channel);

/**
 * @brief 释放所有 PWM 通道（程序退出时调用）
 */
void PWM_DeinitAll(void);

#ifdef __cplusplus
}
#endif

#endif /* __PWM_DRIVER_H */
