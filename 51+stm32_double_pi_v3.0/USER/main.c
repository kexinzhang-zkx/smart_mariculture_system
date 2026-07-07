/**
  * STM32F103 传感器数据采集 + ESP8266 MQTT 发布
  * 
  * 硬件连接:
  *   USART1 (PA9=TX, PA10=RX) → ESP8266
  *   DHT11 → PA15 (P37, 需关JTAG)
  *   光敏电阻 → PA5 (P15) → ADC12_IN5
  * 
  * MQTT: broker 116.62.216.186:1883, topic "gec6818"
  */

#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ======================== 配置参数 ========================= */

#define WIFI_SSID       "sunbao"
#define WIFI_PWD        "12345678"
#define MQTT_BROKER_IP  "118.178.236.156"
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID  "stm32_f1_001"
#define MQTT_TOPIC      "auaculture_sensor"

/* ======================== 海洋牧场模拟传感器 =================== */
/* 正常范围:   DO: 6.0~9.0 mg/L    pH: 7.8~8.3    Turb: 10~60 NTU     */
/* 异常范围:   DO: 2.0~4.0 或 >12   pH: 5.5~6.5 或 >9.5   Turb: >200 */
/* 异常概率:   1/30                                                    */

#define ANOMALY_ODDS      30     /* 1/30 概率出现异常 */
#define SIM_DO_MIN         6.0f
#define SIM_DO_MAX         9.0f
#define SIM_PH_MIN         7.8f
#define SIM_PH_MAX         8.3f
#define SIM_TURB_MIN      10.0f
#define SIM_TURB_MAX      60.0f

/* ======================== 全局变量 ========================= */

float   g_temperature = 0.0f;
float   g_humidity    = 0.0f;
float   g_light_lux   = 0.0f;
float   g_do_val      = 0.0f;
float   g_ph_val      = 0.0f;
float   g_turbidity   = 0.0f;
uint8_t g_sensor_updated = 0;
uint8_t g_mqtt_connected  = 0;

/* ======================== ESP8266 接收缓冲区 ================ */

#define USART1_RX_BUF_SIZE 512
static volatile uint8_t  usart1_rx_buf[USART1_RX_BUF_SIZE];
static volatile uint16_t usart1_rx_idx = 0;

/* ======================== 任务调度器 ======================== */

typedef struct {
    void (*func)(void);
    uint32_t interval;
    uint32_t last_run;
    uint8_t  enabled;
} Task_t;

#define MAX_TASKS 8
static Task_t     task_list[MAX_TASKS];
static uint8_t    task_count = 0;
static volatile uint32_t g_sys_ticks = 0;

/* ======================== SysTick ============================ */

static volatile uint32_t g_delay_tick = 0;

void SysTick_Handler(void)
{
    g_sys_ticks++;
    g_delay_tick++;
}

static void delay_ms(uint32_t nms)
{
    uint32_t start = g_delay_tick;
    while ((g_delay_tick - start) < nms);
}

static void delay_s(uint32_t ns)
{
    while (ns--) delay_ms(1000);
}

/* ======================== DWT 微秒延时 ====================== */

#define DWT_CYCCNT   (*((volatile uint32_t *)0xE0001004))
#define DWT_CTRL     (*((volatile uint32_t *)0xE0001000))
static uint32_t g_cpu_freq_mhz = 72;

static void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_CYCCNT = 0;
    DWT_CTRL  |= 1;
    g_cpu_freq_mhz = SystemCoreClock / 1000000;
}

static void DWT_Delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * g_cpu_freq_mhz;
    while ((DWT_CYCCNT - start) < ticks);
}

static uint32_t DWT_GetCycle(void) { return DWT_CYCCNT; }
static uint32_t DWT_ElapsedUs(uint32_t start) { return (DWT_CYCCNT - start) / g_cpu_freq_mhz; }

/* ======================== DHT11 驱动 ======================== */
/* PA15 (P37) */

#define DHT11_PORT  GPIOA
#define DHT11_PIN   GPIO_Pin_15

static void DHT11_SetOutput(void)
{
    GPIO_InitTypeDef s; GPIO_StructInit(&s);
    s.GPIO_Mode = GPIO_Mode_Out_PP; s.GPIO_Pin = DHT11_PIN; s.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_PORT, &s);
}
static void DHT11_SetInput(void)
{
    GPIO_InitTypeDef s; GPIO_StructInit(&s);
    s.GPIO_Mode = GPIO_Mode_IPU; s.GPIO_Pin = DHT11_PIN;
    GPIO_Init(DHT11_PORT, &s);
}
static uint8_t DHT11_ReadPin(void) { return GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN); }

void DHT11_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    DHT11_SetOutput();
    GPIO_SetBits(DHT11_PORT, DHT11_PIN);
}

uint8_t DHT11_Read(float *temp, float *humi)
{
    uint8_t bytes[5] = {0}, i, j; uint32_t timeout;

    DHT11_SetOutput(); GPIO_ResetBits(DHT11_PORT, DHT11_PIN); DWT_Delay_us(20000); DHT11_SetInput();

    timeout = 0; while (DHT11_ReadPin() == 1) { if (++timeout > 100000) return 1; }
    timeout = 0; while (DHT11_ReadPin() == 0) { if (++timeout > 100000) return 1; }
    timeout = 0; while (DHT11_ReadPin() == 1) { if (++timeout > 100000) return 1; }

    __disable_irq();
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 8; j++) {
            bytes[i] <<= 1; timeout = 0;
            while (DHT11_ReadPin() == 0) { if (++timeout > 100000) goto err; }
            uint32_t t0 = DWT_GetCycle();
            while (DHT11_ReadPin() == 1) { if (DWT_ElapsedUs(t0) > 80) break; }
            if (DWT_ElapsedUs(t0) > 40) bytes[i] |= 1;
        }
    }
    __enable_irq();

    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4]) return 2;
    *humi = (float)bytes[0] + (float)bytes[1] / 10.0f;
    *temp = (float)bytes[2] + (float)bytes[3] / 10.0f;

    DHT11_SetOutput(); GPIO_SetBits(DHT11_PORT, DHT11_PIN); return 0;

err:
    __enable_irq(); DHT11_SetOutput(); GPIO_SetBits(DHT11_PORT, DHT11_PIN); return 1;
}

/* ======================== ADC 光照 ============================ */
/* PA5 (P15) → ADC12_IN5 */

#define ADC_SAMPLES 8
#define LUX_MAX     3000.0f

void ADC_Light_Init(void)
{
    GPIO_InitTypeDef gpio; ADC_InitTypeDef adc;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);
    gpio.GPIO_Pin = GPIO_Pin_5; gpio.GPIO_Mode = GPIO_Mode_AIN; GPIO_Init(GPIOA, &gpio);
    ADC_DeInit(ADC1);
    adc.ADC_Mode = ADC_Mode_Independent; adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE; adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right; adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &adc);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1); while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1); while (ADC_GetCalibrationStatus(ADC1));
}

uint16_t ADC_Read(uint8_t ch)
{
    uint32_t sum = 0;
    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_239Cycles5);
    for (int i = 0; i < ADC_SAMPLES; i++) {
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
        sum += ADC_GetConversionValue(ADC1);
    }
    return (uint16_t)(sum / ADC_SAMPLES);
}

float ADC_ToLux(uint16_t raw)
{
    if (raw >= 4095) return 0.0f; if (raw <= 10) return LUX_MAX;
    float ratio = 1.0f - ((float)raw / 4095.0f);
    return ratio * ratio * LUX_MAX;
}

/* ======================== 模拟传感器 ======================== */
/* 1/30 概率返回非正常数据, 模拟海洋牧场突发环境事件 */

static float _rand_float(float min, float max)
{
    return min + (float)(rand() % 1000) / 1000.0f * (max - min);
}

static void SimSensor(float *do_val, float *ph, float *turb)
{
    if ((rand() % ANOMALY_ODDS) == 0) {
        /* 异常: DO偏低(赤潮/缺氧)或偏高(藻华), pH酸化或碱化, 浊度飙升 */
        switch (rand() % 3) {
            case 0: *do_val = _rand_float(2.0f, 4.0f);  break;  /* 低氧 */
            case 1: *ph     = _rand_float(5.5f, 6.5f);  break;  /* 酸化 */
            case 2: *turb   = _rand_float(200.0f, 500.0f); break; /* 浊度飙升 */
        }
    }
    /* 正常值 (异常时未覆盖的字段仍用正常值) */
    if (*do_val < SIM_DO_MIN || *do_val > SIM_DO_MAX)
        *do_val = _rand_float(SIM_DO_MIN, SIM_DO_MAX);
    if (*ph < SIM_PH_MIN || *ph > SIM_PH_MAX)
        *ph     = _rand_float(SIM_PH_MIN, SIM_PH_MAX);
    if (*turb < SIM_TURB_MIN || *turb > SIM_TURB_MAX)
        *turb   = _rand_float(SIM_TURB_MIN, SIM_TURB_MAX);
}

/* ======================== USART1 (ESP8266) ==================== */
/* PA9=TX, PA10=RX, 中断接收 */

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t ch = (uint8_t)USART_ReceiveData(USART1);
        if (usart1_rx_idx < USART1_RX_BUF_SIZE - 1) {
            usart1_rx_buf[usart1_rx_idx++] = ch;
            usart1_rx_buf[usart1_rx_idx] = '\0';
        }
    }
}

static void USART1_ClearBuf(void) { usart1_rx_idx = 0; memset((uint8_t*)usart1_rx_buf, 0, sizeof(usart1_rx_buf)); }

static uint8_t USART1_CheckResp(const char *expect)
{
    if (expect == NULL) return 0;
    return (strstr((char*)usart1_rx_buf, expect) != NULL) ? 1 : 0;
}

void USART1_SendString(const char *str)
{
    while (*str) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, (uint16_t)(*str++));
    }
}

void USART1_SendCmd(const char *cmd)
{
    USART1_SendString(cmd);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, '\r');
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, '\n');
}

void USART1_Init(uint32_t baud)
{
    GPIO_InitTypeDef gpio; USART_InitTypeDef usart; NVIC_InitTypeDef nvic;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_9; gpio.GPIO_Mode = GPIO_Mode_AF_PP; gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_10; gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate = baud; usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1; usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    nvic.NVIC_IRQChannel = USART1_IRQn; nvic.NVIC_IRQChannelPreemptionPriority = 5;
    nvic.NVIC_IRQChannelSubPriority = 0; nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    USART_Cmd(USART1, ENABLE);
}

/* ======================== ESP8266 初始化 ====================== */
/* 一次性完成 AT→RST→CWMODE→CWJAP→USERCFG→MQTTCONN→SUB */

void ESP8266_Init(void)
{
    char cmd[128];
    uint8_t retry;

    USART1_ClearBuf();

    /* 1. AT 测试 */
    USART1_SendCmd("AT"); delay_ms(1000);
    if (!USART1_CheckResp("OK")) return;

    USART1_ClearBuf(); delay_ms(500);

    /* 2. 重启 */
    USART1_SendCmd("AT+RST"); delay_ms(5000);
    USART1_CheckResp("ready");

    USART1_ClearBuf(); delay_ms(500);

    /* 3. Station 模式 */
    USART1_SendCmd("AT+CWMODE=1"); delay_ms(1000);
    if (!USART1_CheckResp("OK") && !USART1_CheckResp("no change")) return;

    USART1_ClearBuf(); delay_ms(500);

    /* 4. 连接 WiFi */
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PWD);
    USART1_SendCmd(cmd); delay_ms(5000);

    for (retry = 0; retry < 5; retry++) {
        if (USART1_CheckResp("OK") || USART1_CheckResp("WIFI CONNECTED")) break;
        delay_ms(1000);
    }
    if (retry >= 5) return;

    USART1_ClearBuf(); delay_ms(500);

    /* 5. 查看 IP */
    USART1_SendCmd("AT+CIFSR"); delay_ms(1000);
    USART1_CheckResp("+CIFSR");

    USART1_ClearBuf(); delay_ms(500);

    /* 6. MQTT 客户端配置 */
    sprintf(cmd, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"", MQTT_CLIENT_ID, "", "");
    USART1_SendCmd(cmd); delay_ms(1000);
    if (!USART1_CheckResp("OK")) return;

    USART1_ClearBuf(); delay_ms(500);

    /* 7. MQTT 连接服务器 */
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%d,0", MQTT_BROKER_IP, MQTT_BROKER_PORT);
    USART1_SendCmd(cmd); delay_ms(3000);

    for (retry = 0; retry < 5; retry++) {
        if (USART1_CheckResp("OK") || USART1_CheckResp("+MQTTCONNECTED")) break;
        delay_ms(1000);
    }
    if (retry >= 5) { g_mqtt_connected = 0; return; }

    USART1_ClearBuf(); delay_ms(500);

    /* 8. 订阅 */
    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",1", MQTT_TOPIC);
    USART1_SendCmd(cmd); delay_ms(2000);
    USART1_CheckResp("OK") || USART1_CheckResp("+MQTTSUBOK");

    USART1_ClearBuf(); delay_ms(500);

    g_mqtt_connected = 1;
}

/* ======================== 任务函数 ============================ */

/* 任务1: 读取传感器 (2秒) */
void Task_ReadSensor(void)
{
    float temp, humi;
    if (DHT11_Read(&temp, &humi) == 0) {
        g_temperature = temp; g_humidity = humi;
    }
    g_light_lux = ADC_ToLux(ADC_Read(5));
    SimSensor(&g_do_val, &g_ph_val, &g_turbidity);
    g_sensor_updated = 1;
}

/* 任务2: 发布 MQTT (3秒, 非阻塞轮询) */
void Task_PublishMQTT(void)
{
    char cmd[256];
    int i;

    if (!g_sensor_updated || !g_mqtt_connected) return;

    sprintf(cmd,
        "AT+MQTTPUB=0,\"%s\","
        "\"{\\\"temp\\\":%.1f\\,"
        "\\\"humi\\\":%.1f\\,"
        "\\\"light\\\":%.0f\\,"
        "\\\"do\\\":%.2f\\,"
        "\\\"ph\\\":%.2f\\,"
        "\\\"turb\\\":%.1f}"
        "\",0,0",
        MQTT_TOPIC,
        g_temperature, g_humidity, g_light_lux,
        g_do_val, g_ph_val, g_turbidity);

    g_sensor_updated = 0;

    USART1_ClearBuf();
    USART1_SendCmd(cmd);

    /* 轮询等响应, 最多 1.5s */
    for (i = 0; i < 15; i++) {
        delay_ms(100);
        if (USART1_CheckResp("OK") || USART1_CheckResp("+MQTTPUBLISHED")) return;
    }

    /* 超时 → 标记断连 */
    g_mqtt_connected = 0;
}

/* 任务3: 断连检测+重连 (10秒, 仅 mqtt_connected==0 时执行, 每步最小阻塞) */
void Task_Reconnect(void)
{
    char cmd[128];
    int i;

    if (g_mqtt_connected) return;

    /* 1. 检查 WiFi (轮询 1s) */
    USART1_ClearBuf();
    USART1_SendCmd("AT+CWJAP?");
    for (i = 0; i < 10; i++) {
        delay_ms(100);
        if (USART1_CheckResp("+CWJAP:")) break;
    }

    if (i >= 10) {
        /* WiFi 断了, 重连 (不阻塞太久, 分批重试) */
        sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PWD);
        USART1_ClearBuf();
        USART1_SendCmd(cmd);
        for (i = 0; i < 30; i++) {
            delay_ms(500);
            if (USART1_CheckResp("OK") || USART1_CheckResp("WIFI CONNECTED")) break;
        }
        if (i >= 30) return;
    }

    /* 2. 重连 MQTT */
    USART1_ClearBuf();
    USART1_SendCmd("AT+MQTTCLEAN=0");
    for (i = 0; i < 10; i++) { delay_ms(100); if (USART1_CheckResp("OK")) break; }

    USART1_ClearBuf();
    sprintf(cmd, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"", MQTT_CLIENT_ID, "", "");
    USART1_SendCmd(cmd);
    for (i = 0; i < 10; i++) { delay_ms(100); if (USART1_CheckResp("OK")) break; }

    USART1_ClearBuf();
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%d,0", MQTT_BROKER_IP, MQTT_BROKER_PORT);
    USART1_SendCmd(cmd);
    for (i = 0; i < 30; i++) {
        delay_ms(500);
        if (USART1_CheckResp("OK") || USART1_CheckResp("+MQTTCONNECTED")) break;
    }
    if (i >= 30) return;

    /* 订阅 */
    USART1_ClearBuf();
    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",1", MQTT_TOPIC);
    USART1_SendCmd(cmd);
    for (i = 0; i < 10; i++) { delay_ms(100); if (USART1_CheckResp("OK")) break; }

    USART1_ClearBuf();
    g_mqtt_connected = 1;
}

/* ======================== 任务调度 ============================ */

void Scheduler_Init(void)
{
    task_count = 0; g_sys_ticks = 0;
    memset(task_list, 0, sizeof(task_list));
}

int8_t Scheduler_Add(void (*func)(void), uint32_t interval_ms)
{
    if (task_count >= MAX_TASKS) return -1;
    task_list[task_count].func = func;
    task_list[task_count].interval = interval_ms;
    task_list[task_count].last_run = g_sys_ticks;
    task_list[task_count].enabled = 1;
    task_count++;
    return 0;
}

void Scheduler_Run(void)
{
    for (uint8_t i = 0; i < task_count; i++) {
        if (task_list[i].enabled && (g_sys_ticks - task_list[i].last_run >= task_list[i].interval)) {
            task_list[i].last_run = g_sys_ticks;
            task_list[i].func();
        }
    }
}

/* ======================== 系统初始化 ========================== */

void System_Init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    DWT_Init();
    DHT11_Init();
    ADC_Light_Init();
    USART1_Init(115200);
    Scheduler_Init();

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000)) { while (1); }
    NVIC_SetPriority(SysTick_IRQn, NVIC_EncodePriority(NVIC_PriorityGroup_2, 1, 0));

    Scheduler_Add(Task_ReadSensor,  3000);
    Scheduler_Add(Task_PublishMQTT, 3000);
    Scheduler_Add(Task_Reconnect,  10000);
}

/* ========================= 主函数 ============================== */

int main(void)
{
    System_Init();

    delay_s(1);

    /* 一次性完成 ESP8266 初始化 */
    ESP8266_Init();

    while (1) {
        Scheduler_Run();
        delay_ms(10);
    }
}
