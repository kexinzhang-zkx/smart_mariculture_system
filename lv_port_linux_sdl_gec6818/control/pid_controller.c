/**
 * ============================================================
 *  PID 控制算法实现
 * ============================================================
 *
 *  位置式 PID 公式:
 *    u(k) = Kp*e(k) + Ki*Σe(i)*T + Kd*(e(k)-e(k-1))/T
 *
 *  积分分离:
 *    当 |e(k)| > integral_limit 时，关闭积分项（防止超调）
 *
 *  输出限幅:
 *    output ∈ [output_min, output_max]
 */

#include "pid_controller.h"
#include <string.h>
#include <math.h>

/* ---- 全局 PID 控制器数组 ---- */
PID_Controller g_pid[DEVICE_COUNT];

/* ========== PID 初始化 ========== */
void PID_Init(DeviceID dev, float kp, float ki, float kd, float out_max)
{
    if (dev >= DEVICE_COUNT) return;

    PID_Controller *p = &g_pid[dev];

    /* 参数配置 */
    p->params.kp             = kp;
    p->params.ki             = ki;
    p->params.kd             = kd;
    p->params.integral_limit = 30.0f;    /* 误差超过30%关闭积分 */
    p->params.output_max     = out_max;
    p->params.output_min     = 0.0f;
    p->params.dead_zone      = 0.5f;     /* 死区0.5% */

    /* 内部状态清零 */
    p->setpoint    = 0.0f;
    p->last_error  = 0.0f;
    p->prev_error  = 0.0f;
    p->integral    = 0.0f;
    p->output      = 0.0f;
    p->enabled     = 1;                  /* 默认自动模式 */
    p->last_time_ms = 0;
}

/* ========== PID 核心计算 ========== */
float PID_Compute(DeviceID dev, float setpoint, float measurement, uint32_t dt_ms)
{
    if (dev >= DEVICE_COUNT) return 0.0f;

    PID_Controller *p = &g_pid[dev];

    p->setpoint = setpoint;

    /* 手动模式直接返回当前输出 */
    if (!p->enabled) {
        return p->output;
    }

    /* 计算误差 */
    float error = setpoint - measurement;

    /* ---- 死区处理：误差在死区内不调节 ---- */
    if (fabsf(error) < p->params.dead_zone) {
        return p->output;   /* 保持上一次输出 */
    }

    /* ---- 比例项 ---- */
    float p_term = p->params.kp * error;

    /* ---- 积分项（带分离抗饱和）---- */
    if (fabsf(error) < p->params.integral_limit) {
        /* 误差小，开启积分 */
        float dt_sec = (dt_ms > 0) ? (dt_ms / 1000.0f) : 0.1f;
        p->integral += error * dt_sec;

        /* 积分限幅 */
        float i_max = p->params.output_max / (p->params.ki + 0.001f);
        if (p->integral >  i_max) p->integral =  i_max;
        if (p->integral < -i_max) p->integral = -i_max;
    } else {
        /* 误差大，关闭积分（防止积分饱和导致超调） */
        p->integral = 0.0f;
    }
    float i_term = p->params.ki * p->integral;

    /* ---- 微分项（不完全微分，抑制高频噪声）---- */
    float dt_sec = (dt_ms > 0) ? (dt_ms / 1000.0f) : 0.1f;
    float d_term = p->params.kd * (error - p->last_error) / dt_sec;

    /* ---- 合成输出 ---- */
    float output = p_term + i_term + d_term;

    /* ---- 输出限幅 ---- */
    if (output > p->params.output_max) {
        output = p->params.output_max;
        /* 遇限削弱积分 */
        if (error > 0) p->integral -= error * dt_sec * 0.5f;
    }
    if (output < p->params.output_min) {
        output = p->params.output_min;
        if (error < 0) p->integral -= error * dt_sec * 0.5f;
    }

    /* ---- 保存状态 ---- */
    p->prev_error = p->last_error;
    p->last_error = error;
    p->output     = output;
    p->last_time_ms += dt_ms;

    return output;
}

/* ========== 在线修改 PID 参数 ========== */
void PID_SetParams(DeviceID dev, float kp, float ki, float kd)
{
    if (dev >= DEVICE_COUNT) return;

    PID_Controller *p = &g_pid[dev];
    p->params.kp = kp;
    p->params.ki = ki;
    p->params.kd = kd;

    /* 参数变更时重置积分，防止突变冲击 */
    p->integral = 0.0f;
}

/* ========== 切换 自动/手动 模式 ========== */
void PID_SetMode(DeviceID dev, uint8_t auto_mode)
{
    if (dev >= DEVICE_COUNT) return;

    g_pid[dev].enabled = auto_mode;

    /* 模式切换时重置状态 */
    if (auto_mode) {
        PID_Reset(dev);
    } else {
        /* 关闭时清零输出，设备立即停止 */
        g_pid[dev].output = 0.0f;
    }
}

/* ========== 重置控制器 ========== */
void PID_Reset(DeviceID dev)
{
    if (dev >= DEVICE_COUNT) return;

    PID_Controller *p = &g_pid[dev];
    p->last_error = 0.0f;
    p->prev_error = 0.0f;
    p->integral   = 0.0f;
    p->output     = 0.0f;
}

/* ========== 手动设置输出 ========== */
void PID_SetManualOutput(DeviceID dev, float duty)
{
    if (dev >= DEVICE_COUNT) return;

    PID_Controller *p = &g_pid[dev];

    p->enabled = 0;   /* 强制切换到手动模式 */
    p->output  = duty;

    /* 限制范围 */
    if (p->output > p->params.output_max) p->output = p->params.output_max;
    if (p->output < p->params.output_min) p->output = p->params.output_min;
}
