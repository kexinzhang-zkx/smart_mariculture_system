/**
 * ============================================================
 *  自动控制引擎实现
 * ============================================================
 *
 *  闭环控制链路：
 *    ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
 *    │ 传感器采集 │ → │ 阈值判断  │ → │ PID 计算  │ → │ PWM 输出 │
 *    │ (snapshot)│    │ (threshold)│   │ (pid.c)  │    │ (pwm.c)  │
 *    └──────────┘    └──────────┘    └──────────┘    └─────┬────┘
 *         ↑                                                │
 *         └──────────── 反馈回读 ──────────────────────────┘
 */

#include "auto_control.h"
#include "pid_controller.h"
#include "pwm_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

/* ========== 全局状态变量 ========== */
DeviceState    g_device_state[3];
EnvThresholds  g_thresholds;
SensorSnapshot g_sensor_snapshot;
ControlMode    g_control_mode = MODE_FULL_AUTO;
volatile int   g_control_loop_running = 0;

/* ---- 内部控制循环周期 (ms) ---- */
#define CONTROL_PERIOD_MS   200     /* 200ms 控制周期 = 5Hz */

/* ---- 是否连接了真实传感器（STM32 UART）---- */
int g_sensor_connected = 0;

/* ---- 设备名称 ---- */
static const char *device_names[] = { "增氧机", "投喂电机", "循环水泵" };

/* ========== 默认环境阈值（水产养殖典型值）========== */
static void init_default_thresholds(void)
{
    /* ---- 溶解氧：养殖水体健康 DO > 5mg/L ---- */
    g_thresholds.do_low_threshold  = 2.0f;   /* DO < 2mg/L → 缺氧，开增氧机 */
    g_thresholds.do_high_threshold = 9.0f;   /* DO > 9mg/L → 已充足，关增氧机 */
    g_thresholds.do_target         = 5.0f;   /* PID 目标：维持 5mg/L */

    /* ---- 水温：适宜范围 22~28°C ---- */
    g_thresholds.temp_high_threshold = 35.0f; /* 水温 > 35°C → 开水泵降温 */
    g_thresholds.temp_low_threshold  = 15.0f; /* 水温 < 15°C → 开水泵升温 */
    g_thresholds.temp_target         = 25.0f; /* PID 目标：25°C */

    /* ---- 投喂：每 60 秒投喂 10 秒（演示用，正式部署改回240分钟）---- */
    g_thresholds.feed_interval_min  = 1;      /* 1 分钟间隔 (演示) */
    g_thresholds.feed_duration_sec  = 10;     /* 投喂 10 秒 */
    g_thresholds.feed_motor_speed   = 60.0f;  /* 投喂转速 60% */
}

/* ========== 控制引擎初始化 ========== */
void AutoControl_Init(void)
{
    printf("[SYS] 养殖自动化控制引擎 v1.0\n");

    /* 1. 设置默认阈值 */
    init_default_thresholds();

    /* 2. 初始化 PID 控制器 */
    PID_Init(DEVICE_AERATOR, 1.5f, 0.3f, 0.05f, 100.0f);
    PID_Init(DEVICE_WATER_PUMP, 1.0f, 0.2f, 0.0f, 100.0f);
    PID_Init(DEVICE_FEEDER, 0.8f, 0.1f, 0.0f, 100.0f);

    for (int i = 0; i < DEVICE_COUNT; i++) {
        PID_SetMode(i, 0);
    }

    /* 3. 初始化 PWM 硬件 */
    int pwm_ok = PWM_InitAll();

    /* 4. 初始化设备状态 */
    memset(g_device_state, 0, sizeof(g_device_state));
    for (int i = 0; i < 3; i++) {
        g_device_state[i].auto_mode = 1;
    }

    /* 5. 初始化传感器快照 */
    memset(&g_sensor_snapshot, 0, sizeof(g_sensor_snapshot));

    printf("[SYS] 引擎就绪 (PWM:%s)\n", pwm_ok > 0 ? "OK" : "模拟");
}

/* ========== 传感器数据更新（线程安全）========== */
void AutoControl_UpdateSensors(const SensorSnapshot *snapshot)
{
    if (snapshot == NULL) return;

    /* 原子拷贝（32bit 架构上单次赋值是原子的） */
    memcpy((void *)&g_sensor_snapshot, snapshot, sizeof(SensorSnapshot));
}

/* ========== 模拟传感器数据 ========== */
void AutoControl_SimulateSensors(void)
{
    static uint32_t tick = 0;
    tick++;

    /* 基于正弦波叠加模拟日变化 */
    float hour_factor = sinf((float)(tick % 7200) / 7200.0f * 2.0f * 3.14159f); /* 2小时周期 */

    /* 溶解氧：日间光合作用增加 DO，夜间呼吸降低 */
    float do_base = 4.5f + hour_factor * 2.0f;

    /* 水温：日间升温 */
    float temp_base = 26.0f + hour_factor * 3.0f;

    /* pH：稳定在 7.0 附近 */
    float ph_base = 7.2f + sinf((float)tick / 3600.0f) * 0.3f;

    g_sensor_snapshot.air_temp      = 25.0f + hour_factor * 5.0f;
    g_sensor_snapshot.air_humidity  = 65.0f + hour_factor * 15.0f;
    g_sensor_snapshot.water_temp    = temp_base;
    g_sensor_snapshot.light_lux     = 5000.0f + hour_factor * 30000.0f;
    g_sensor_snapshot.ph_value      = ph_base;
    g_sensor_snapshot.turbidity_ntu = 15.0f + (rand() % 100) / 100.0f * 5.0f;
    g_sensor_snapshot.dissolved_o2  = do_base;
    g_sensor_snapshot.timestamp_ms  = tick * CONTROL_PERIOD_MS;

    g_sensor_snapshot.do_valid   = 1;
    g_sensor_snapshot.temp_valid = 1;
    g_sensor_snapshot.ph_valid   = 1;
}

/* ========== 单个设备控制逻辑 ========== */
static void control_aerator(SensorSnapshot *sensor)
{
    DeviceState *state = &g_device_state[DEVICE_AERATOR];

    if (!g_pid[DEVICE_AERATOR].enabled) {
        /* 手动模式 */
        state->duty_cycle = g_pid[DEVICE_AERATOR].output;
    } else if (sensor->do_valid) {
        /* 自动 PID 模式 */
        float duty = PID_Compute(DEVICE_AERATOR,
                                  g_thresholds.do_target,
                                  sensor->dissolved_o2,
                                  CONTROL_PERIOD_MS);
        state->target_duty = duty;

        /* 阈值判断：DO 低于下限 → 强制启动 */
        if (sensor->dissolved_o2 < g_thresholds.do_low_threshold) {
            /* 缺氧紧急：全速运行 */
            duty = 100.0f;
            state->target_duty = 100.0f;
        } else if (sensor->dissolved_o2 > g_thresholds.do_high_threshold) {
            /* 氧气充足：停止 */
            duty = 0.0f;
            state->target_duty = 0.0f;
        }

        state->duty_cycle = duty;
        state->running = (duty > 1.0f) ? 1 : 0;

        /* 估算 RPM (假设最大 3000 RPM) */
        state->rpm = (duty / 100.0f) * 3000.0f;
    } else {
        /* 传感器无效 → 安全模式：全速运行保氧 */
        state->duty_cycle = 60.0f;
        state->running = 1;
        state->rpm = 1800.0f;
    }

    /* 输出到 PWM */
    PWM_SetDuty(PWM_AERATOR_CHANNEL, state->duty_cycle);
}

static void control_water_pump(SensorSnapshot *sensor)
{
    DeviceState *state = &g_device_state[DEVICE_WATER_PUMP];

    if (!g_pid[DEVICE_WATER_PUMP].enabled) {
        state->duty_cycle = g_pid[DEVICE_WATER_PUMP].output;
    } else if (sensor->temp_valid) {
        /* 水温越高 → 需要越强循环 → 误差取 当前温度-目标温度 */
        float duty = PID_Compute(DEVICE_WATER_PUMP,
                                  sensor->water_temp,        /* 当前值当"setpoint" */
                                  g_thresholds.temp_target,   /* 目标值当"measurement" */
                                  CONTROL_PERIOD_MS);
        state->target_duty = duty;

        /* 温度异常：启动水泵促进水体循环 */
        if (sensor->water_temp > g_thresholds.temp_high_threshold ||
            sensor->water_temp < g_thresholds.temp_low_threshold) {
            /* 温度异常：启动水泵促进水体循环 */
            if (duty < 40.0f) duty = 40.0f;
        }

        state->duty_cycle = duty;
        state->running = (duty > 1.0f) ? 1 : 0;
        state->rpm = (duty / 100.0f) * 2500.0f;
    } else {
        state->duty_cycle = 20.0f;   /* 安全低速 */
        state->running = 1;
        state->rpm = 500.0f;
    }

    PWM_SetDuty(PWM_WATERPUMP_CHANNEL, state->duty_cycle);
}

static void control_feeder(SensorSnapshot *sensor)
{
    DeviceState *state = &g_device_state[DEVICE_FEEDER];

    /* 投喂电机是非连续工作模式：定时投喂 */
    static uint32_t feeder_timer_sec    = 0;
    static uint32_t last_feed_time_sec  = 0;

    if (!g_pid[DEVICE_FEEDER].enabled) {
        state->duty_cycle = g_pid[DEVICE_FEEDER].output;
    } else {
        /* 定时投喂逻辑 */
        uint32_t now_sec = sensor->timestamp_ms / 1000;

        if (now_sec - last_feed_time_sec >= g_thresholds.feed_interval_min * 60) {
            /* 到达投喂时间 → 启动投喂 */
            feeder_timer_sec   = g_thresholds.feed_duration_sec;
            last_feed_time_sec = now_sec;
            printf("[CTRL] 投喂 %ds\n", g_thresholds.feed_duration_sec);
        }

        if (feeder_timer_sec > 0) {
            feeder_timer_sec -= CONTROL_PERIOD_MS / 1000;
            state->duty_cycle = g_thresholds.feed_motor_speed;
            state->running = 1;
            state->rpm = (g_thresholds.feed_motor_speed / 100.0f) * 1500.0f;
        } else {
            state->duty_cycle = 0.0f;
            state->running = 0;
            state->rpm = 0.0f;
        }
    }

    PWM_SetDuty(PWM_FEEDER_CHANNEL, state->duty_cycle);
}

/* ========== 累计运行时间 ========== */
static void update_runtime(void)
{
    for (int i = 0; i < 3; i++) {
        if (g_device_state[i].running) {
            g_device_state[i].run_time_sec += CONTROL_PERIOD_MS / 1000;
        }
    }
}

/* ========== 控制循环主函数 ========== */
void *AutoControl_Loop(void *arg)
{
    (void)arg;

    g_control_loop_running = 1;

    uint32_t loop_count = 0;

    while (g_control_loop_running) {
        /* ---- 第1步：采集 ---- */
        if (!g_sensor_connected) {
            AutoControl_SimulateSensors();
        }

        SensorSnapshot sensor;
        memcpy(&sensor, (void *)&g_sensor_snapshot, sizeof(SensorSnapshot));

        /* ---- 第2步：判断 + 第3步：执行 ---- */
        switch (g_control_mode) {
            case MODE_FULL_AUTO:
                control_aerator(&sensor);
                control_water_pump(&sensor);
                control_feeder(&sensor);
                break;

            case MODE_THRESHOLD:
                /* 纯阈值模式：不做 PID 调节，仅根据阈值启停 */
                control_aerator(&sensor);
                control_water_pump(&sensor);
                control_feeder(&sensor);
                break;

            case MODE_MANUAL:
                /* 手动模式：PID 计算已停用，直接输出用户设定值 */
                PWM_SetDuty(PWM_AERATOR_CHANNEL, g_pid[DEVICE_AERATOR].output);
                PWM_SetDuty(PWM_WATERPUMP_CHANNEL, g_pid[DEVICE_WATER_PUMP].output);
                PWM_SetDuty(PWM_FEEDER_CHANNEL, g_pid[DEVICE_FEEDER].output);
                break;
        }

        /* ---- 第4步：反馈（更新运行时间）---- */
        update_runtime();

        loop_count++;

        /* 日志（每 60 秒一次） */
        if (loop_count % 300 == 0) {   /* 300 * 200ms = 60s */
            printf("[CTRL] DO=%.1f | 增氧%.0f%% 泵%.0f%% 投喂=%s\n",
                   sensor.dissolved_o2,
                   g_device_state[DEVICE_AERATOR].duty_cycle,
                   g_device_state[DEVICE_WATER_PUMP].duty_cycle,
                   g_device_state[DEVICE_FEEDER].running ? "ON" : "OFF");
            fflush(stdout);
        }

        /* 休眠 200ms */
        usleep(CONTROL_PERIOD_MS * 1000);
    }

    return NULL;
}

/* ========== 停止控制循环 ========== */
void AutoControl_Stop(void)
{
    g_control_loop_running = 0;
}

/* ========== 设置控制模式 ========== */
void AutoControl_SetMode(ControlMode mode)
{
    g_control_mode = mode;

    if (mode == MODE_MANUAL) {
        /* 切换到手动时，关闭所有 PID 自动模式 */
        for (int i = 0; i < DEVICE_COUNT; i++) {
            PID_SetMode(i, 0);
        }
    } else {
        /* 恢复自动时，重置并开启 PID */
        for (int i = 0; i < DEVICE_COUNT; i++) {
            PID_Reset(i);
            PID_SetMode(i, 1);
        }
    }
}

/* ========== 获取设备名称 ========== */
const char *AutoControl_DeviceName(int dev_id)
{
    if (dev_id < 0 || dev_id >= 3) return "未知";
    return device_names[dev_id];
}
