/**
 * ============================================================
 *  PID 控制算法模块 — 水产养殖精准自动化调控
 * ============================================================
 *
 *  特性：
 *    - 位置式 PID + 增量式 PID 双模式
 *    - 积分分离抗饱和 (anti-windup)
 *    - 输出限幅保护
 *    - 死区控制（避免小误差频繁调节）
 *    - 在线参数调节（对接 LVGL UI）
 *
 *  适用设备：
 *    - 增氧机 (aerator)
 *    - 投喂电机 (feeder_motor)
 *    - 循环水泵 (water_pump)
 */

#ifndef __PID_CONTROLLER_H
#define __PID_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ========== 设备 ID 枚举 ========== */
typedef enum {
    DEVICE_AERATOR    = 0,   /* 增氧机    — 关联溶解氧传感器 */
    DEVICE_FEEDER     = 1,   /* 投喂电机  — 关联定时/投喂量 */
    DEVICE_WATER_PUMP = 2,   /* 循环水泵  — 关联水温传感器 */
    DEVICE_COUNT      = 3
} DeviceID;

/* ========== PID 参数结构体 ========== */
typedef struct {
    float kp;                 /* 比例系数 */
    float ki;                 /* 积分系数 */
    float kd;                 /* 微分系数 */

    /* ---- 抗饱和/限幅参数 ---- */
    float integral_limit;     /* 积分分离阈值（误差超过此值关闭积分） */
    float output_max;         /* 输出上限 (PWM 占空比: 0~100) */
    float output_min;         /* 输出下限 */

    /* ---- 死区 ---- */
    float dead_zone;          /* 死区宽度（误差在死区内不调节） */
} PID_Params;

/* ========== PID 控制器运行状态 ========== */
typedef struct {
    PID_Params params;        /* 参数副本（可在运行中修改） */

    /* ---- 内部状态 ---- */
    float setpoint;           /* 目标值（如目标溶解氧 = 5.0 mg/L）*/
    float last_error;         /* 上一次误差 e(k-1) */
    float prev_error;         /* 上上次误差 e(k-2) */
    float integral;           /* 积分累加量 */
    float output;             /* 当前输出 (0~100% 占空比) */

    uint8_t enabled;          /* 使能标志：1=自动PID调节, 0=手动模式 */
    uint32_t last_time_ms;    /* 上次计算时间戳（用于 delta_T） */
} PID_Controller;

/* ========== 全局 PID 控制器实例 ========== */
extern PID_Controller g_pid[DEVICE_COUNT];

/* ========== 函数声明 ========== */

/**
 * @brief 初始化指定设备的 PID 控制器
 * @param dev    设备 ID
 * @param kp     比例系数
 * @param ki     积分系数
 * @param kd     微分系数
 * @param out_max 输出上限 (0~100)
 *
 * 典型参数（需现场整定）：
 *   增氧机:   Kp=1.5, Ki=0.3, Kd=0.05
 *   循环水泵: Kp=1.0, Ki=0.2, Kd=0.00
 *   投喂电机: Kp=0.8, Ki=0.1, Kd=0.00
 */
void PID_Init(DeviceID dev, float kp, float ki, float kd, float out_max);

/**
 * @brief PID 控制器核心计算
 * @param dev         设备 ID
 * @param setpoint    设定目标值
 * @param measurement 当前实测值（传感器读数）
 * @param dt_ms       距上次调用时间（毫秒）
 * @return PWM 占空比 (0.0 ~ 100.0)
 *
 * 调用周期：建议 100~500ms
 */
float PID_Compute(DeviceID dev, float setpoint, float measurement, uint32_t dt_ms);

/**
 * @brief 在线修改 PID 参数（UI 回调使用）
 */
void PID_SetParams(DeviceID dev, float kp, float ki, float kd);

/**
 * @brief 切换 自动/手动 模式
 */
void PID_SetMode(DeviceID dev, uint8_t auto_mode);

/**
 * @brief 重置控制器内部状态（模式切换/传感器故障恢复时调用）
 */
void PID_Reset(DeviceID dev);

/**
 * @brief 直接设置输出（手动模式）
 * @param duty 占空比 0.0~100.0
 */
void PID_SetManualOutput(DeviceID dev, float duty);

#ifdef __cplusplus
}
#endif

#endif /* __PID_CONTROLLER_H */
