/**
 * ============================================================
 *  自动控制引擎 — "采集→判断→执行→反馈" 闭环
 * ============================================================
 *
 *  工作流程：
 *    1. 读取全局传感器数据 (g_env_snapshot)
 *    2. 遍历每个设备，判断是否触发阈值
 *    3. 触发 → PID 计算目标占空比 → PWM 输出
 *    4. 记录执行结果，供 UI 查询
 *
 *  运行方式：
 *    独立 pthread，每 200ms 执行一次控制循环
 *
 *  断网自治：
 *    所有逻辑在本地完成，不依赖任何网络服务
 */

#ifndef __AUTO_CONTROL_H
#define __AUTO_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ========== 控制模式 ========== */
typedef enum {
    MODE_FULL_AUTO  = 0,   /* 全自动：PID 闭环调节 */
    MODE_THRESHOLD  = 1,   /* 阈值开关：仅根据阈值启停 */
    MODE_MANUAL     = 2    /* 全手动：人工设定占空比 */
} ControlMode;

/* ========== 设备运行状态 ========== */
typedef struct {
    float duty_cycle;         /* 当前 PWM 占空比 (%) */
    float target_duty;        /* PID 计算目标占空比 (%) */
    int   running;            /* 运行标志：1=运行中, 0=停止 */
    float rpm;                /* 估算转速 (RPM) */
    uint32_t run_time_sec;    /* 累计运行时间 (秒) */
    uint8_t auto_mode;        /* 1=自动模式, 0=手动模式 */
} DeviceState;

/* ========== 环境阈值配置 ========== */
typedef struct {
    /* ---- 溶解氧 (关联增氧机) ---- */
    float do_low_threshold;       /* DO 低于此值 → 启动增氧机 */
    float do_high_threshold;      /* DO 高于此值 → 停止增氧机 */
    float do_target;              /* PID 目标溶解氧值 */

    /* ---- 水温 (关联循环水泵) ---- */
    float temp_high_threshold;    /* 水温高于此值 → 启动循环水泵 */
    float temp_low_threshold;     /* 水温低于此值 → 停止循环水泵 */
    float temp_target;            /* PID 目标水温 */

    /* ---- 投喂控制 ---- */
    uint32_t feed_interval_min;   /* 投喂间隔 (分钟) */
    uint32_t feed_duration_sec;   /* 单次投喂持续时间 (秒) */
    float    feed_motor_speed;    /* 投喂电机转速百分比 */

} EnvThresholds;

/* ========== 传感器数据快照（供控制循环读取）========== */
typedef struct {
    float air_temp;            /* 空气温度 (°C) */
    float air_humidity;        /* 空气湿度 (%RH) */
    float water_temp;          /* 水温 (°C) */
    float light_lux;           /* 光照 (lx) */
    float ph_value;            /* pH */
    float turbidity_ntu;       /* 浊度 (NTU) */
    float dissolved_o2;        /* 溶解氧 (mg/L) */
    uint32_t timestamp_ms;     /* 采样时间戳 */

    /* 数据有效性标志 */
    uint8_t do_valid : 1;
    uint8_t temp_valid : 1;
    uint8_t ph_valid : 1;
} SensorSnapshot;

/* ========== 全局状态（UI 线程可读取）========== */
extern DeviceState    g_device_state[3];     /* 0=增氧机, 1=投喂电机, 2=循环水泵 */
extern EnvThresholds  g_thresholds;          /* 环境阈值 */
extern SensorSnapshot g_sensor_snapshot;     /* 传感器数据快照 */
extern ControlMode    g_control_mode;        /* 当前控制模式 */
extern volatile int   g_control_loop_running; /* 控制循环运行标志 */
extern int            g_sensor_connected;      /* 1=STM32已连接, 0=未连接(用模拟数据) */

/* ========== 函数声明 ========== */

/**
 * @brief 初始化控制引擎
 *        设置默认阈值、初始化 PID 控制器、初始化 PWM
 */
void AutoControl_Init(void);

/**
 * @brief 控制循环主函数（在独立 pthread 中运行）
 *        循环执行：读取传感器 → 判断阈值 → PID计算 → PWM输出
 * @param arg 未使用
 * @return NULL
 */
void *AutoControl_Loop(void *arg);

/**
 * @brief 停止控制循环
 */
void AutoControl_Stop(void);

/**
 * @brief 更新传感器数据快照（由 UART 接收线程或模拟模块调用）
 *        线程安全：内部使用原子写入
 */
void AutoControl_UpdateSensors(const SensorSnapshot *snapshot);

/**
 * @brief 模拟传感器数据（STM32 未接入时使用）
 *        基于时间生成合理的仿真数据
 */
void AutoControl_SimulateSensors(void);

/**
 * @brief 设置控制模式
 */
void AutoControl_SetMode(ControlMode mode);

/**
 * @brief 获取设备友好的名称
 */
const char *AutoControl_DeviceName(int dev_id);

#ifdef __cplusplus
}
#endif

#endif /* __AUTO_CONTROL_H */
