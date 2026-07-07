/**
 * ============================================================
 *  PWM 电机驱动实现 — Linux sysfs PWM
 * ============================================================
 *
 *  使用方法：
 *    1. PWM_InitAll()       — 初始化 PWM 通道
 *    2. PWM_SetDuty(ch, %)  — 设置占空比
 *    3. PWM_DeinitAll()     — 程序退出时释放
 *
 *  调试：
 *    # cat /sys/class/pwm/pwmchip0/pwm0/duty_cycle
 *    # cat /sys/kernel/debug/pwm   (如果内核启用了 DEBUG_FS)
 */

#include "pwm_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ================================================================
 *  板级适配：根据 GEC6818 实际 PWM 控制器路径修改此宏
 *  ------------------------------------------------------------
 *  S5P6818 的 PWM 通常在 pwmchip0 (TIMER PWM) 或 pwmchip1 (SoC PWM)
 *  查看: ls /sys/class/pwm/
 * ================================================================ */
#define PWM_CHIP_PATH   "/sys/class/pwm/pwmchip0"

/* ---- 内部：已初始化的通道位掩码 ---- */
static int g_pwm_initialized_mask = 0x00;

/* ========== 写整数值到 sysfs 文件 ========== */
static int sysfs_write_int(const char *path, int value)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "[PWM] ERROR: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(fp, "%d", value);
    fclose(fp);
    return 0;
}

/* ========== 写字符串到 sysfs 文件 ========== */
static int sysfs_write_str(const char *path, const char *value)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "[PWM] ERROR: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(fp, "%s", value);
    fclose(fp);
    return 0;
}

/* ========== 读整数值从 sysfs 文件 ========== */
static int sysfs_read_int(const char *path, int *value)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    int ret = fscanf(fp, "%d", value);
    fclose(fp);
    return (ret == 1) ? 0 : -1;
}

/* ========== 内部：获取通道路径前缀 ========== */
static void pwm_ch_path(int channel, char *buf, size_t bufsize, const char *attr)
{
    snprintf(buf, bufsize, PWM_CHIP_PATH "/pwm%d/%s", channel, attr);
}

/* ========== 导出并初始化单个 PWM 通道 ========== */
static int pwm_channel_init(int channel)
{
    /* 检查是否已导出 */
    char path[256];
    snprintf(path, sizeof(path), PWM_CHIP_PATH "/pwm%d", channel);

    if (access(path, F_OK) != 0) {
        /* 未导出 → 执行 export */
        char export_path[256];
        snprintf(export_path, sizeof(export_path), PWM_CHIP_PATH "/export");

        if (sysfs_write_int(export_path, channel) != 0) {
            fprintf(stderr, "[PWM] ERROR: export channel %d failed\n", channel);
            return -1;
        }

        /* 等待 sysfs 节点创建 */
        usleep(100000);  /* 100ms */
    }

    /* 设置周期 (1kHz = 1,000,000 ns) */
    pwm_ch_path(channel, path, sizeof(path), "period");
    if (sysfs_write_int(path, PWM_PERIOD_NS) != 0) {
        fprintf(stderr, "[PWM] ERROR: set period for pwm%d failed\n", channel);
        return -1;
    }

    /* 初始占空比设为 0 */
    pwm_ch_path(channel, path, sizeof(path), "duty_cycle");
    if (sysfs_write_int(path, 0) != 0) {
        fprintf(stderr, "[PWM] ERROR: set duty_cycle=0 for pwm%d failed\n", channel);
        return -1;
    }

    /* 使能 PWM 输出 */
    pwm_ch_path(channel, path, sizeof(path), "enable");
    if (sysfs_write_int(path, 1) != 0) {
        fprintf(stderr, "[PWM] ERROR: enable pwm%d failed\n", channel);
        return -1;
    }

    return 0;
}

/* ========== 初始化所有 PWM 通道 ========== */
int PWM_InitAll(void)
{
    int success_count = 0;
    int channels[] = {
        PWM_AERATOR_CHANNEL,
        PWM_FEEDER_CHANNEL,
        PWM_WATERPUMP_CHANNEL
    };
    const char *names[] = { "增氧机", "投喂电机", "循环水泵" };
    int n = sizeof(channels) / sizeof(channels[0]);

    for (int i = 0; i < n; i++) {
        if (pwm_channel_init(channels[i]) == 0) {
            g_pwm_initialized_mask |= (1 << channels[i]);
            success_count++;
        }
    }
    printf("[PWM] %d/%d OK\n", success_count, n);
    return success_count;
}

/* ========== 设置占空比 ========== */
int PWM_SetDuty(int channel, float duty_percent)
{
    /* 范围保护 */
    if (duty_percent < 0.0f)   duty_percent = 0.0f;
    if (duty_percent > 100.0f) duty_percent = 100.0f;

    /* 检查是否已初始化 */
    if (!(g_pwm_initialized_mask & (1 << channel))) {
        /* 未初始化 → 静默跳过（PWM 硬件可能不可用） */
        return -1;
    }

    /* duty_cycle_ns = period_ns * duty_percent / 100 */
    int duty_ns = (int)((float)PWM_PERIOD_NS * duty_percent / 100.0f);

    char path[256];
    pwm_ch_path(channel, path, sizeof(path), "duty_cycle");

    return sysfs_write_int(path, duty_ns);
}

/* ========== 开启/关闭 PWM ========== */
int PWM_Enable(int channel, int on)
{
    if (!(g_pwm_initialized_mask & (1 << channel))) {
        return -1;
    }

    char path[256];
    pwm_ch_path(channel, path, sizeof(path), "enable");

    return sysfs_write_int(path, on ? 1 : 0);
}

/* ========== 读取当前占空比 ========== */
float PWM_GetDuty(int channel)
{
    if (!(g_pwm_initialized_mask & (1 << channel))) {
        return -1.0f;
    }

    char path[256];
    pwm_ch_path(channel, path, sizeof(path), "duty_cycle");

    int duty_ns = 0;
    if (sysfs_read_int(path, &duty_ns) != 0) {
        return -1.0f;
    }

    return (float)duty_ns / (float)PWM_PERIOD_NS * 100.0f;
}

/* ========== 释放所有 PWM 通道 ========== */
void PWM_DeinitAll(void)
{
    for (int ch = 0; ch < 5; ch++) {
        if (g_pwm_initialized_mask & (1 << ch)) {
            char path[256];
            pwm_ch_path(ch, path, sizeof(path), "enable");
            sysfs_write_int(path, 0);
            snprintf(path, sizeof(path), PWM_CHIP_PATH "/unexport");
            sysfs_write_int(path, ch);
            g_pwm_initialized_mask &= ~(1 << ch);
        }
    }
}
