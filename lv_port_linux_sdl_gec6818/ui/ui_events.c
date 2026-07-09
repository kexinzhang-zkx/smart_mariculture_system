// SquareLine Studio — ui_events.c (含 PID+PWM 闭环控制)

#include "ui.h"
#include "ui_helpers.h"
#include "../control/auto_control.h"
#include "../control/pid_controller.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>

// ==================== DeepSeek AI 模块 (Ollama HTTP) ====================

#include <sys/socket.h>
#include <arpa/inet.h>

#define OLLAMA_PORT       11434
#define OLLAMA_IP         "192.168.29.23"
#define OLLAMA_MODEL      "deepseek-r1:1.5b"
#define AI_BUF_SIZE       4096 * 10
#define AI_RESPONSE_MAX   2048

/* 全局：AI 异步响应 */
static char      g_ai_response[AI_RESPONSE_MAX];
static volatile int g_ai_ready = 0;    /* 0=空闲, 1=有结果待显示, -1=错误 */

/* ---- 从 Ollama JSON 响应中提取 "response" 字段 ---- */
static int ai_parse_response(const char *json, char *out, int out_size)
{
    const char *key = "\"response\":\"";
    const char *start = strstr(json, key);
    if (!start) {
        /* 可能是错误或其他格式 */
        const char *err_key = "\"error\":\"";
        start = strstr(json, err_key);
        if (start) {
            start += strlen(err_key);
            int i = 0;
            while (*start && *start != '"' && i < out_size - 1) {
                if (*start == '\\' && *(start+1)) start++;
                else out[i++] = *start;
                start++;
            }
            out[i] = '\0';
        } else {
            snprintf(out, out_size, "回复故障");
        }
        return -1;
    }
    start += strlen(key);

    int i = 0;
    while (*start && i < out_size - 1) {
        if (*start == '\\' && *(start+1) == '"') {
            start += 2;
            out[i++] = '"';
        } else if (*start == '\\' && *(start+1) == 'n') {
            start += 2; /* 跳过 \n，不输出 */
        } else if (*start == '\\' && *(start+1) == '\\') {
            start += 2;
            out[i++] = '\\';
        } else if (*start == '"') {
            break;   /* 字符串结束 */
        } else {
            out[i++] = *start;
            start++;
        }
    }
    out[i] = '\0';

    /* 过滤 DeepSeek R1 的 <｜end▁of▁thinking｜> 标签 */
    {   /* 去掉 <think>...</think> */
        char *p, *q;
        while (1) {
            p = strstr(out, "<think>");
            if (!p) break;
            q = strstr(p, "</think>");
            if (!q) break;
            q += 8;
            memmove(p, q, strlen(q) + 1);
        }
    }
    {   /* 去掉换行 */
        char *r = out, *w = out;
        while (*r) { if (*r == '\n' || *r == '\r') r++; else *w++ = *r++; }
        *w = '\0';
    }

    return i > 0 ? 0 : -1;
}


/* ---- 线程函数：socket 发送 HTTP 请求 ---- */
static void *ai_ollama_thread(void *arg)
{
    char *prompt = (char *)arg;
    int sock;
    struct sockaddr_in serv_addr;
    char body[2048];
    char header[512];
    char request_full[4096];
    char *recv_buf;

    /* 构造 body */
    int blen = snprintf(body, sizeof(body),
        "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false}",
        OLLAMA_MODEL, prompt);
    if (blen < 0 || blen >= (int)sizeof(body)) blen = (int)sizeof(body) - 1;
    /* 用 strlen 代替 snprintf 返回值：确保 Content-Length = 实际字节数 */
    int body_len = (int)strlen(body);
    printf("[AI] snprintf返回=%d strlen=%d\n", blen, body_len); fflush(stdout);
    printf("[AI] BODY: %s\n", body); fflush(stdout);

    /* 构造 header */
    int hlen = snprintf(header, sizeof(header),
        "POST /api/generate HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        OLLAMA_IP, OLLAMA_PORT, body_len);

    /* 拼接 */
    memcpy(request_full, header, hlen);
    memcpy(request_full + hlen, body, body_len);
    int req_len = hlen + body_len;
    printf("[AI] CL=%d total=%d\n", body_len, req_len); fflush(stdout);

    /* socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { snprintf(g_ai_response, AI_RESPONSE_MAX, "系统故障"); g_ai_ready=-1; free(prompt); return NULL; }
    struct timeval tv = {60, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(OLLAMA_PORT);
    inet_pton(AF_INET, OLLAMA_IP, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("[AI] connect fail\n"); fflush(stdout);
        snprintf(g_ai_response, AI_RESPONSE_MAX, "系统故障"); g_ai_ready=-1;
        close(sock); free(prompt); return NULL;
    }

    /* send */
    int sent = 0;
    while (sent < req_len) {
        int n = send(sock, request_full + sent, req_len - sent, 0);
        if (n <= 0) break;
        sent += n;
    }
    printf("[AI] sent %d/%d\n", sent, req_len); fflush(stdout);

    /* recv */
    recv_buf = malloc(8192);
    int total = 0;
    if (recv_buf) {
        while (total < 8191) { int n=recv(sock,recv_buf+total,8191-total,0); if(n<=0)break; total+=n; }
    }
    close(sock);
    printf("[AI] recv %d bytes\n", total); fflush(stdout);

    if (total > 0) {
        recv_buf[total] = '\0';
        ai_parse_response(recv_buf, g_ai_response, AI_RESPONSE_MAX);
        g_ai_ready = 1;
    } else {
        snprintf(g_ai_response, AI_RESPONSE_MAX, "系统故障"); g_ai_ready = -1;
    }
    free(recv_buf); free(prompt);
    return NULL;
}

static void ai_ollama_ask(const char *prompt_fmt, ...)
{
    /* 先设置"思考中"，g_ai_ready=1 让定时器立即刷新到屏幕 */
    snprintf(g_ai_response, AI_RESPONSE_MAX, "AI分析");
    g_ai_ready = 1;

    /* 格式化 prompt */
    char *prompt = malloc(1024);
    if (!prompt) {
        snprintf(g_ai_response, AI_RESPONSE_MAX, "内存故障");
        g_ai_ready = -1;
        return;
    }

    va_list args;
    va_start(args, prompt_fmt);
    vsnprintf(prompt, 1024, prompt_fmt, args);
    va_end(args);

    printf("[AI] 提问: %s\n", prompt);
    fflush(stdout);

    /* 发到线程里执行 */
    pthread_t tid;
    int ret = pthread_create(&tid, NULL, ai_ollama_thread, prompt);
    if (ret != 0) {
        printf("[AI] 线程创建失败! err=%d\n", ret); fflush(stdout);
        snprintf(g_ai_response, AI_RESPONSE_MAX, "系统故障");
        g_ai_ready = -1;
        free(prompt);
        return;
    }
    pthread_detach(tid);
    printf("[AI] 线程已创建\n"); fflush(stdout);
}

/* ---- Screen4 按钮：一键评价 ---- */
void ai_btn_evaluate(lv_event_t *e)
{
    (void)e;
    ai_ollama_ask("DO=%.1f,T=%.1f,pH=%.2f,Turb=%.1f,Lux=%.0f。评价水质,30字。",
        g_sensor_snapshot.dissolved_o2, g_sensor_snapshot.water_temp,
        g_sensor_snapshot.ph_value, g_sensor_snapshot.turbidity_ntu,
        g_sensor_snapshot.light_lux);
}

void ai_btn_diagnose(lv_event_t *e)
{
    (void)e;
    ai_ollama_ask("增氧%.0f%% 泵%.0f%% 投喂%d DO=%.1f T=%.1f。有故障吗?20字。",
        g_device_state[DEVICE_AERATOR].duty_cycle,
        g_device_state[DEVICE_WATER_PUMP].duty_cycle,
        g_device_state[DEVICE_FEEDER].running,
        g_sensor_snapshot.dissolved_o2, g_sensor_snapshot.water_temp);
}

void ai_btn_suggest(lv_event_t *e)
{
    (void)e;
    ai_ollama_ask("DO=%.1f,T=%.1f,pH=%.2f,Turb=%.1f。养殖建议,30字。",
        g_sensor_snapshot.dissolved_o2, g_sensor_snapshot.water_temp,
        g_sensor_snapshot.ph_value, g_sensor_snapshot.turbidity_ntu);
}

void ai_btn_maintain(lv_event_t *e)
{
    (void)e;
    ai_ollama_ask("增氧%us(%.0f%%) 泵%us(%.0f%%) 投喂%us(%.0f%%)。维护建议,30字。",
        g_device_state[DEVICE_AERATOR].run_time_sec, g_device_state[DEVICE_AERATOR].duty_cycle,
        g_device_state[DEVICE_WATER_PUMP].run_time_sec, g_device_state[DEVICE_WATER_PUMP].duty_cycle,
        g_device_state[DEVICE_FEEDER].run_time_sec, g_device_state[DEVICE_FEEDER].duty_cycle);
}

// ==================== 登录模块 ====================

#define USER_FILE      "./user_info.txt"
#define USER_NAME_MAX  20
#define USER_PWD_MAX   20

// ==================== 视频模块 ====================

#define PIPE_PATH      "/home/gec/pipe"
#define VIDEO_PATH     "/mnt/udisk/mv/mv.mp4"

static int video_started = 0;

typedef struct {
    char name[USER_NAME_MAX];
    char pwd[USER_PWD_MAX];
} UserInfo;

static int is_user_exist(const char *username)
{
    FILE *fp = fopen(USER_FILE, "r");
    if (fp == NULL) {
        FILE *empty_fp = fopen(USER_FILE, "w");
        if (empty_fp) fclose(empty_fp);
        return 0;
    }
    UserInfo temp;
    while (fscanf(fp, "%s %s", temp.name, temp.pwd) == 2) {
        if (strcmp(temp.name, username) == 0) { fclose(fp); return 1; }
    }
    fclose(fp);
    return 0;
}

static int register_user(const char *username, const char *password)
{
    if (is_user_exist(username)) return 0;
    FILE *fp = fopen(USER_FILE, "a");
    if (fp == NULL) { perror("创建文件失败"); return 0; }
    fprintf(fp, "%s %s\n", username, password);
    fclose(fp);
    return 1;
}

static int login_verify(const char *username, const char *password)
{
    if (!is_user_exist(username)) return -1;
    FILE *fp = fopen(USER_FILE, "r");
    UserInfo temp;
    while (fscanf(fp, "%s %s", temp.name, temp.pwd) == 2) {
        if (strcmp(temp.name, username) == 0 && strcmp(temp.pwd, password) == 0) {
            fclose(fp); return 1;
        }
    }
    fclose(fp);
    return 0;
}

// ==================== 传感器空占位 ====================

void temp_sensor1(lv_event_t * e) {}
void temp_sensor2(lv_event_t * e) {}
void light_sensor(lv_event_t * e) {}
void hum_sensor(lv_event_t * e) {}
void press_sensor(lv_event_t * e) {}

// ==================== 登录回调 ====================

void LOGIN_TASK(lv_event_t *e)
{
    const char *account = lv_textarea_get_text(ui_TextArea1);
    const char *password = lv_textarea_get_text(ui_TextArea2);
    if (strlen(account) == 0 || strlen(password) == 0) {
        lv_label_set_text(ui_Label2, "Please Input Info");
        return;
    }
    int ret = login_verify(account, password);
    if (ret == -1) {
        printf("[LOGIN] '%s' 未注册\n", account);
        lv_label_set_text(ui_Label2, "Please Register First");
    } else if (ret == 0) {
        printf("[LOGIN] '%s' 密码错误\n", account);
        lv_label_set_text(ui_Label2, "Login Failed");
    } else {
        printf("[LOGIN] '%s' 登录成功\n", account);
        lv_label_set_text(ui_Label2, "Login Success");
        lv_textarea_set_text(ui_TextArea1, "");
        lv_textarea_set_text(ui_TextArea2, "");
        _ui_screen_change(&ui_Screen7, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, &ui_Screen7_screen_init);
    }
}

void REGISTER_TASK(lv_event_t *e)
{
    const char *account = lv_textarea_get_text(ui_TextArea1);
    const char *password = lv_textarea_get_text(ui_TextArea2);
    if (strlen(account) == 0 || strlen(password) == 0) {
        lv_label_set_text(ui_Label2, "Empty Input");
        return;
    }
    if (register_user(account, password)) {
        printf("[LOGIN] '%s' 注册成功\n", account);
        lv_label_set_text(ui_Label2, "Register Success");
        lv_textarea_set_text(ui_TextArea1, "");
        lv_textarea_set_text(ui_TextArea2, "");
    } else {
        lv_label_set_text(ui_Label2, "Account Exists");
    }
}

// ==================== 视频播放器 ====================

void *play_task(void *arg)
{
    (void)arg;
    printf("[VIDEO] 正在启动 mplayer...\n");
    printf("[VIDEO] 视频路径: %s\n", VIDEO_PATH);
    mkfifo(PIPE_PATH, 0777);
    FILE *fp = popen("mplayer -slave -quiet -geometry 0:0 -zoom -x 600 -y 480 "
                     "-input file=" PIPE_PATH " " VIDEO_PATH " 2>&1", "r");
    if (fp == NULL) { perror("popen mplayer failed"); video_started = 0; return NULL; }
    printf("[VIDEO] mplayer 已启动\n");
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp) != NULL) printf("mplayer: %s", buf);
    int ret = pclose(fp);
    printf("[VIDEO] mplayer 退出, 返回值=%d\n", ret);
    video_started = 0;
    return NULL;
}

void VIDEO_CONTINUE(lv_event_t * e) {
    if (!video_started) {
        printf("[BTN] 继续 → 启动播放器\n");
        video_started = 1;
        pthread_t tid; pthread_create(&tid, NULL, play_task, NULL); pthread_detach(tid);
    } else { printf("[BTN] 继续 → 恢复播放\n"); system("killall -CONT mplayer"); }
}
void VIDEO_PAUSE(lv_event_t * e)        { printf("[BTN] 暂停\n"); system("killall -STOP mplayer"); }
void VIDEO_FAST_FORWARD(lv_event_t * e)  { printf("[BTN] 快进 +10s\n"); system("echo 'seek +10 0' > " PIPE_PATH); }
void VIDEO_FAST_BACKWARD(lv_event_t * e) { printf("[BTN] 快退 -10s\n"); system("echo 'seek -10 0' > " PIPE_PATH); }
void VIDEO_VOLUME_UP(lv_event_t * e)    { printf("[BTN] 音量+\n"); system("echo 'volume 10 0' > " PIPE_PATH); }
void VIDEO_VOLUME_DOWN(lv_event_t * e)  { printf("[BTN] 音量-\n"); system("echo 'volume -10 0' > " PIPE_PATH); }
void VIDEO_STOP(lv_event_t * e)         { printf("[BTN] 停止播放\n"); system("killall mplayer"); video_started = 0; }

// ==================== 图片浏览器 ====================

lv_obj_t *ui_Image19 = NULL;
static const char *img_paths[] = {"A:/2.bmp", "A:/3.bmp"};
#define IMG_COUNT 2
static int img_index = 0;
static int img_zoom = 256;

static void img_ensure_created(void)
{
    if (ui_Image19 != NULL) return;
    lv_obj_t *active = lv_screen_active();
    if (active == NULL) return;
    ui_Image19 = lv_image_create(active);
    lv_obj_set_width(ui_Image19, 600);
    lv_obj_set_height(ui_Image19, 400);
    lv_obj_set_x(ui_Image19, -20); lv_obj_set_y(ui_Image19, -10);
    lv_obj_set_align(ui_Image19, LV_ALIGN_CENTER);
    lv_image_set_src(ui_Image19, img_paths[0]);
    lv_image_set_scale(ui_Image19, 256);
    printf("[IMG] ui_Image19 created (lazy init on Screen9)\n");
}

void IMG_PREV(lv_event_t * e) {
    img_ensure_created(); if (!ui_Image19) return;
    img_index = (img_index - 1 + IMG_COUNT) % IMG_COUNT;
    lv_image_set_src(ui_Image19, img_paths[img_index]);
    lv_image_set_scale(ui_Image19, img_zoom);
}
void IMG_NEXT(lv_event_t * e) {
    img_ensure_created(); if (!ui_Image19) return;
    img_index = (img_index + 1) % IMG_COUNT;
    lv_image_set_src(ui_Image19, img_paths[img_index]);
    lv_image_set_scale(ui_Image19, img_zoom);
}
void IMG_ZOOM_IN(lv_event_t * e) {
    img_ensure_created(); if (!ui_Image19) return;
    if (img_zoom < 1024) { img_zoom += 32;
        lv_image_set_src(ui_Image19, img_paths[img_index]);
        lv_image_set_scale(ui_Image19, img_zoom); lv_obj_invalidate(ui_Image19); }
}
void IMG_ZOOM_OUT(lv_event_t * e) {
    img_ensure_created(); if (!ui_Image19) return;
    if (img_zoom > 128) { img_zoom -= 32;
        lv_image_set_src(ui_Image19, img_paths[img_index]);
        lv_image_set_scale(ui_Image19, img_zoom); lv_obj_invalidate(ui_Image19); }
}
void IMG_BACK(lv_event_t * e) {
    img_index = 0; img_zoom = 256;
    _ui_screen_change(&ui_Screen7, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_Screen7_screen_init);
}

// ==================== 蓝牙串口模块 ====================

#define BT_DEV     "/dev/ttySAC1"
static int bt_fd = -1;
static volatile int bt_cmd = 0;
static lv_timer_t *bt_timer = NULL;

static int bt_uart_init(int fd)
{
    struct termios new_uart;
    bzero(&new_uart, sizeof(new_uart));
    tcgetattr(fd, &new_uart); cfmakeraw(&new_uart);
    new_uart.c_cflag |= CLOCAL | CREAD;
    cfsetospeed(&new_uart, B9600); cfsetispeed(&new_uart, B9600);
    new_uart.c_cflag &= ~CSIZE; new_uart.c_cflag |= CS8;
    new_uart.c_cflag &= ~PARENB; new_uart.c_cflag &= ~CSTOPB;
    new_uart.c_cc[VTIME] = 0; new_uart.c_cc[VMIN] = 1;
    new_uart.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tcflush(fd, TCIOFLUSH); tcsetattr(fd, TCSANOW, &new_uart);
    return 0;
}

static void *bt_recv_thread(void *arg)
{
    (void)arg; char buf[64];
    while (1) {
        bzero(buf, sizeof(buf));
        int len = read(bt_fd, buf, sizeof(buf) - 1);
        if (len > 0) {
            printf("[BT] recv: %s\n", buf); fflush(stdout);
            if (strstr(buf, "prev"))      bt_cmd = 1;
            else if (strstr(buf, "next")) bt_cmd = 2;
            else if (strstr(buf, "large")) bt_cmd = 3;
            else if (strstr(buf, "small")) bt_cmd = 4;
        }
    }
    return NULL;
}

static void bt_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    /* 蓝牙指令全局响应，不限制特定页面 */
    img_ensure_created(); if (!ui_Image19) return;
    if (bt_cmd == 1) {
        img_index = (img_index - 1 + IMG_COUNT) % IMG_COUNT;
        lv_image_set_src(ui_Image19, img_paths[img_index]);
        lv_image_set_scale(ui_Image19, img_zoom);
        lv_obj_invalidate(lv_obj_get_parent(ui_Image19));
    } else if (bt_cmd == 2) {
        img_index = (img_index + 1) % IMG_COUNT;
        lv_image_set_src(ui_Image19, img_paths[img_index]);
        lv_image_set_scale(ui_Image19, img_zoom);
        lv_obj_invalidate(lv_obj_get_parent(ui_Image19));
    } else if (bt_cmd == 3) {
        if (img_zoom < 1024) {
            img_zoom += 32;
            lv_image_set_src(ui_Image19, img_paths[img_index]);
            lv_image_set_scale(ui_Image19, img_zoom); lv_obj_invalidate(ui_Image19);
        }
    } else if (bt_cmd == 4) {
        if (img_zoom > 128) {
            img_zoom -= 32;
            lv_image_set_src(ui_Image19, img_paths[img_index]);
            lv_image_set_scale(ui_Image19, img_zoom); lv_obj_invalidate(ui_Image19);
        }
    }
    bt_cmd = 0;
}

void BT_INIT(void)
{
    if (bt_fd >= 0) return;
    bt_fd = open(BT_DEV, O_RDWR | O_NOCTTY);
    if (bt_fd == -1) { perror("[BT] Open UART error"); return; }
    bt_uart_init(bt_fd);
    printf("[BT] UART open success, fd=%d, baud=9600\n", bt_fd); fflush(stdout);
    pthread_t tid; pthread_create(&tid, NULL, bt_recv_thread, NULL); pthread_detach(tid);
    if (bt_timer == NULL) bt_timer = lv_timer_create(bt_timer_cb, 100, NULL);
    usleep(100000); write(bt_fd, "AT\r\n", 4);
    usleep(100000); write(bt_fd, "AT+NAME=sjyzkx\r\n", 16);
}

// ==================== Screen2 PID+PWM 闭环控制 ====================

#define KP_STEP 0.1f
#define KI_STEP 0.05f
#define KD_STEP 0.01f

/* ---- Screen2 动态标签（定时器创建，避免全局变量冲突）---- */
static lv_obj_t *s2_aer_kp, *s2_aer_ki, *s2_aer_kd;
static lv_obj_t *s2_pump_kp, *s2_pump_ki, *s2_pump_kd;
static lv_obj_t *s2_feed_kp, *s2_feed_ki, *s2_feed_kd;
static lv_obj_t *s2_aer_duty, *s2_pump_duty, *s2_feed_duty;
static lv_obj_t *s2_do_val, *s2_do_target, *s2_temp_val, *s2_temp_target;
static int s2_labels_inited = 0;

static void s2_create_labels(void)
{
    if (s2_labels_inited) return;

    /* Container1: 设备占空比 → 右侧 */
    s2_aer_duty = lv_label_create(ui_Container1);
    lv_obj_set_size(s2_aer_duty, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_aer_duty, 111, -18);
    lv_obj_set_align(s2_aer_duty, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_aer_duty, &ui_font_chinese28, 0);

    s2_pump_duty = lv_label_create(ui_Container1);
    lv_obj_set_size(s2_pump_duty, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_pump_duty, 115, 21);
    lv_obj_set_align(s2_pump_duty, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_pump_duty, &ui_font_chinese28, 0);

    s2_feed_duty = lv_label_create(ui_Container1);
    lv_obj_set_size(s2_feed_duty, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_feed_duty, 116, 59);
    lv_obj_set_align(s2_feed_duty, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_feed_duty, &ui_font_chinese28, 0);

    /* Container4: PID参数值 (3行×3列) */
    s2_aer_kp = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_aer_kp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_aer_kp, -112, -11);
    lv_obj_set_align(s2_aer_kp, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_aer_kp, &ui_font_chinese28, 0);

    s2_aer_ki = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_aer_ki, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_aer_ki, 57, -10);
    lv_obj_set_align(s2_aer_ki, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_aer_ki, &ui_font_chinese28, 0);

    s2_aer_kd = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_aer_kd, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_aer_kd, 213, -8);
    lv_obj_set_align(s2_aer_kd, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_aer_kd, &ui_font_chinese28, 0);

    s2_pump_kp = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_pump_kp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_pump_kp, -112, 28);
    lv_obj_set_align(s2_pump_kp, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_pump_kp, &ui_font_chinese28, 0);

    s2_pump_ki = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_pump_ki, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_pump_ki, 57, 26);
    lv_obj_set_align(s2_pump_ki, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_pump_ki, &ui_font_chinese28, 0);

    s2_pump_kd = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_pump_kd, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_pump_kd, 216, 25);
    lv_obj_set_align(s2_pump_kd, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_pump_kd, &ui_font_chinese28, 0);

    s2_feed_kp = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_feed_kp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_feed_kp, -111, 61);
    lv_obj_set_align(s2_feed_kp, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_feed_kp, &ui_font_chinese28, 0);

    s2_feed_ki = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_feed_ki, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_feed_ki, 61, 64);
    lv_obj_set_align(s2_feed_ki, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_feed_ki, &ui_font_chinese28, 0);

    s2_feed_kd = lv_label_create(ui_Container4);
    lv_obj_set_size(s2_feed_kd, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_feed_kd, 217, 61);
    lv_obj_set_align(s2_feed_kd, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_feed_kd, &ui_font_chinese28, 0);

    /* Container5: 实时数据 */
    s2_do_val = lv_label_create(ui_Container5);
    lv_obj_set_size(s2_do_val, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_do_val, 22, -22);
    lv_obj_set_align(s2_do_val, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_do_val, &ui_font_chinese28, 0);

    s2_do_target = lv_label_create(ui_Container5);
    lv_obj_set_size(s2_do_target, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_do_target, 47, 9);
    lv_obj_set_align(s2_do_target, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_do_target, &ui_font_chinese28, 0);

    s2_temp_val = lv_label_create(ui_Container5);
    lv_obj_set_size(s2_temp_val, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_temp_val, 45, 37);
    lv_obj_set_align(s2_temp_val, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_temp_val, &ui_font_chinese28, 0);

    s2_temp_target = lv_label_create(ui_Container5);
    lv_obj_set_size(s2_temp_target, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(s2_temp_target, 46, 65);
    lv_obj_set_align(s2_temp_target, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(s2_temp_target, &ui_font_chinese28, 0);

    s2_labels_inited = 1;
}

static void pid_label_refresh(int dev)
{
    PID_Controller *p = &g_pid[dev];
    lv_obj_t *kp=NULL, *ki=NULL, *kd=NULL;
    char b[16];

    if (dev == DEVICE_AERATOR)      { kp=s2_aer_kp;  ki=s2_aer_ki;  kd=s2_aer_kd; }
    else if (dev == DEVICE_WATER_PUMP) { kp=s2_pump_kp; ki=s2_pump_ki; kd=s2_pump_kd; }
    else                            { kp=s2_feed_kp; ki=s2_feed_ki; kd=s2_feed_kd; }

    if(kp){ snprintf(b,16,"%.2f",p->params.kp); lv_label_set_text(kp,b); }
    if(ki){ snprintf(b,16,"%.2f",p->params.ki); lv_label_set_text(ki,b); }
    if(kd){ snprintf(b,16,"%.2f",p->params.kd); lv_label_set_text(kd,b); }
}

static void pid_adj(int dev, char param, float delta)
{
    PID_Controller *p = &g_pid[dev];
    float v;
    switch (param) {
        case 'P': v = p->params.kp + delta; if(v<0)v=0;
                  PID_SetParams(dev, v, p->params.ki, p->params.kd); break;
        case 'I': v = p->params.ki + delta; if(v<0)v=0;
                  PID_SetParams(dev, p->params.kp, v, p->params.kd); break;
        case 'D': v = p->params.kd + delta; if(v<0)v=0;
                  PID_SetParams(dev, p->params.kp, p->params.ki, v); break;
    }
    pid_label_refresh(dev);
}

/* ---- 增氧机 PID ---- */
void aer_kp_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_AERATOR,'P', KP_STEP); }
void aer_kp_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_AERATOR,'P',-KP_STEP); }
void aer_ki_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_AERATOR,'I', KI_STEP); }
void aer_ki_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_AERATOR,'I',-KI_STEP); }
void aer_kd_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_AERATOR,'D', KD_STEP); }
void aer_kd_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_AERATOR,'D',-KD_STEP); }

/* ---- 循环水泵 PID ---- */
void pump_kp_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_WATER_PUMP,'P', KP_STEP); }
void pump_kp_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_WATER_PUMP,'P',-KP_STEP); }
void pump_ki_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_WATER_PUMP,'I', KI_STEP); }
void pump_ki_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_WATER_PUMP,'I',-KI_STEP); }
void pump_kd_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_WATER_PUMP,'D', KD_STEP); }
void pump_kd_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_WATER_PUMP,'D',-KD_STEP); }

/* ---- 投喂电机 PID ---- */
void feed_kp_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_FEEDER,'P', KP_STEP); }
void feed_kp_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_FEEDER,'P',-KP_STEP); }
void feed_ki_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_FEEDER,'I', KI_STEP); }
void feed_ki_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_FEEDER,'I',-KI_STEP); }
void feed_kd_up(lv_event_t *e) { (void)e; pid_adj(DEVICE_FEEDER,'D', KD_STEP); }
void feed_kd_dn(lv_event_t *e) { (void)e; pid_adj(DEVICE_FEEDER,'D',-KD_STEP); }

/* ---- 设备开关 ---- */
void sw_aerator(lv_event_t *e) {
    (void)e;
    int on = lv_obj_has_state(ui_Switch1, LV_STATE_CHECKED);
    printf("[CTRL] 增氧机 → %s\n", on ? "自动" : "关闭");
    PID_SetMode(DEVICE_AERATOR, on);
}
void sw_pump(lv_event_t *e) {
    (void)e;
    int on = lv_obj_has_state(ui_Switch4, LV_STATE_CHECKED);
    printf("[CTRL] 水泵 → %s\n", on ? "自动" : "关闭");
    PID_SetMode(DEVICE_WATER_PUMP, on);
}
void sw_feeder(lv_event_t *e) {
    (void)e;
    int on = lv_obj_has_state(ui_Switch3, LV_STATE_CHECKED);
    printf("[CTRL] 投喂 → %s\n", on ? "自动" : "关闭");
    PID_SetMode(DEVICE_FEEDER, on);
}

/* ---- Screen2 目标值调节 ---- */

#define TARGET_DO_STEP   0.5f
#define TARGET_TEMP_STEP 1.0f

void do_target_up(lv_event_t *e)
{
    (void)e;
    g_thresholds.do_target += TARGET_DO_STEP;
    printf("[TARGET] 溶解氧目标 → %.1f mg/L\n", g_thresholds.do_target);
}

void do_target_dn(lv_event_t *e)
{
    (void)e;
    g_thresholds.do_target -= TARGET_DO_STEP;
    if (g_thresholds.do_target < 0) g_thresholds.do_target = 0;
    printf("[TARGET] 溶解氧目标 → %.1f mg/L\n", g_thresholds.do_target);
}

void temp_target_up(lv_event_t *e)
{
    (void)e;
    g_thresholds.temp_target += TARGET_TEMP_STEP;
    printf("[TARGET] 水温目标 → %.1f °C\n", g_thresholds.temp_target);
}

void temp_target_dn(lv_event_t *e)
{
    (void)e;
    g_thresholds.temp_target -= TARGET_TEMP_STEP;
    if (g_thresholds.temp_target < 0) g_thresholds.temp_target = 0;
    printf("[TARGET] 水温目标 → %.1f °C\n", g_thresholds.temp_target);
}

/* ---- 500ms 定时刷新 Screen2 + Screen6 ---- */
void ui_S2_update_timer(lv_timer_t *t)
{
    (void)t;

    lv_obj_t *active = lv_screen_active();
    char buf[32];
    static int call_count = 0;
    call_count++;

    /* === 数据持久化 (全局, 每30秒写一次) === */
    static int log_tick = 0;
    log_tick++;
    if (log_tick % 60 == 0) {
        FILE *fp = fopen("./sensor.log", "a");
        if (fp) {
            fprintf(fp, "%.1f,%.1f,%.1f,%.1f,%.1f,%.0f\n",
                g_sensor_snapshot.water_temp,
                g_sensor_snapshot.air_humidity,
                g_sensor_snapshot.dissolved_o2,
                g_sensor_snapshot.ph_value,
                g_sensor_snapshot.turbidity_ntu,
                g_sensor_snapshot.light_lux);
            fclose(fp);
        } else {
            static int err_once = 0;
            if (!err_once) { printf("[SYS] sensor.log 写入失败!\n"); fflush(stdout); err_once = 1; }
        }
    }

    /* ========== Screen2：PID控制面板 ========== */
    if (active == ui_Screen2 && ui_Screen2 != NULL) {

        s2_create_labels();  /* 首次激活时创建动态标签 */

        pid_label_refresh(DEVICE_AERATOR);
        pid_label_refresh(DEVICE_WATER_PUMP);
        pid_label_refresh(DEVICE_FEEDER);

        if(s2_do_val){ snprintf(buf, sizeof(buf), "%.1f mg/L", g_sensor_snapshot.dissolved_o2); lv_label_set_text(s2_do_val, buf); }
        if(s2_do_target){ snprintf(buf, sizeof(buf), "%.1f", g_thresholds.do_target); lv_label_set_text(s2_do_target, buf); }
        if(s2_temp_val){ snprintf(buf, sizeof(buf), "%.1f℃", g_sensor_snapshot.water_temp); lv_label_set_text(s2_temp_val, buf); }
        if(s2_temp_target){ snprintf(buf, sizeof(buf), "%.1f", g_thresholds.temp_target); lv_label_set_text(s2_temp_target, buf); }
        if(s2_aer_duty){ snprintf(buf, sizeof(buf), "D:%.0f%% P:%.0f%%", g_device_state[DEVICE_AERATOR].duty_cycle, g_pid[DEVICE_AERATOR].output); lv_label_set_text(s2_aer_duty, buf); }
        if(s2_pump_duty){ snprintf(buf, sizeof(buf), "D:%.0f%% P:%.0f%%", g_device_state[DEVICE_WATER_PUMP].duty_cycle, g_pid[DEVICE_WATER_PUMP].output); lv_label_set_text(s2_pump_duty, buf); }
        if(s2_feed_duty){ snprintf(buf, sizeof(buf), "D:%.0f%% P:%.0f%%", g_device_state[DEVICE_FEEDER].duty_cycle, g_pid[DEVICE_FEEDER].output); lv_label_set_text(s2_feed_duty, buf); }
    }

    /* ========== Screen6：传感器实时数据 ========== */
    if (active == ui_Screen6 && ui_Screen6 != NULL) {
        static lv_obj_t *s6[6]={0};
        if(!s6[0]){
            s6[0]=lv_label_create(ui_Container3);lv_obj_set_pos(s6[0],30,-105);lv_obj_set_align(s6[0],LV_ALIGN_CENTER);lv_obj_set_style_text_font(s6[0],&ui_font_chinese28,0);
            s6[1]=lv_label_create(ui_Container3);lv_obj_set_pos(s6[1],30,-60);lv_obj_set_align(s6[1],LV_ALIGN_CENTER);lv_obj_set_style_text_font(s6[1],&ui_font_chinese28,0);
            s6[2]=lv_label_create(ui_Container3);lv_obj_set_pos(s6[2],30,-15);lv_obj_set_align(s6[2],LV_ALIGN_CENTER);lv_obj_set_style_text_font(s6[2],&ui_font_chinese28,0);
            s6[3]=lv_label_create(ui_Container3);lv_obj_set_pos(s6[3],30,30);lv_obj_set_align(s6[3],LV_ALIGN_CENTER);lv_obj_set_style_text_font(s6[3],&ui_font_chinese28,0);
            s6[4]=lv_label_create(ui_Container3);lv_obj_set_pos(s6[4],30,75);lv_obj_set_align(s6[4],LV_ALIGN_CENTER);lv_obj_set_style_text_font(s6[4],&ui_font_chinese28,0);
            s6[5]=lv_label_create(ui_Container3);lv_obj_set_pos(s6[5],30,115);lv_obj_set_align(s6[5],LV_ALIGN_CENTER);lv_obj_set_style_text_font(s6[5],&ui_font_chinese28,0);
        }
        snprintf(buf,sizeof(buf),"%.1fC",g_sensor_snapshot.water_temp);lv_label_set_text(s6[0],buf);
        snprintf(buf,sizeof(buf),"%.1f%%",g_sensor_snapshot.air_humidity);lv_label_set_text(s6[1],buf);
        snprintf(buf,sizeof(buf),"%.2fmg",g_sensor_snapshot.dissolved_o2);lv_label_set_text(s6[2],buf);
        snprintf(buf,sizeof(buf),"%.2f",g_sensor_snapshot.ph_value);lv_label_set_text(s6[3],buf);
        snprintf(buf,sizeof(buf),"%.1fNT",g_sensor_snapshot.turbidity_ntu);lv_label_set_text(s6[4],buf);
        snprintf(buf,sizeof(buf),"%.0flx",g_sensor_snapshot.light_lux);lv_label_set_text(s6[5],buf);

        /* 折线图数据推送 */
        extern lv_obj_t * chart_s6;
        extern lv_chart_series_t * chart_ser[6];
        if (chart_s6 && chart_ser[0]) {
            lv_chart_set_next_value(chart_s6, chart_ser[0], (int32_t)(g_sensor_snapshot.water_temp * 2));
            lv_chart_set_next_value(chart_s6, chart_ser[1], (int32_t)(g_sensor_snapshot.air_humidity));
            lv_chart_set_next_value(chart_s6, chart_ser[2], (int32_t)(g_sensor_snapshot.dissolved_o2 * 10));
            lv_chart_set_next_value(chart_s6, chart_ser[3], (int32_t)(g_sensor_snapshot.ph_value * 10));
            lv_chart_set_next_value(chart_s6, chart_ser[4], (int32_t)(g_sensor_snapshot.turbidity_ntu));
            lv_chart_set_next_value(chart_s6, chart_ser[5], (int32_t)(g_sensor_snapshot.light_lux / 100));
        }
    }

    /* ========== Screen5：数据预处理分析 ========== */
    if (active == ui_Screen5 && ui_Screen5 != NULL) {

        /* === 20个静态指针 — 不受Screen2/6全局变量覆盖 === */
        /* Container6 实时数据质量: 5个 */
        static lv_obj_t *s5_do    = NULL; /* 溶解氧 (-12,-44) */
        static lv_obj_t *s5_temp  = NULL; /* 水温   (-11,-12) */
        static lv_obj_t *s5_ph    = NULL; /* pH     (-12,17)  */
        static lv_obj_t *s5_turb  = NULL; /* 浊度   (-11,51)  */
        static lv_obj_t *s5_stat  = NULL; /* 状态   (-7,88)   */
        /* Container8 24h统计: 6个 DO列+Temp列 max/min/avg */
        static lv_obj_t *s5_do_max  = NULL; /* DO最大值 (25,-38) */
        static lv_obj_t *s5_do_min  = NULL; /* DO最小值 (25,2)   */
        static lv_obj_t *s5_do_avg  = NULL; /* DO平均值 (25,42)  */
        static lv_obj_t *s5_t_max   = NULL; /* T最大值  (115,-38)*/
        static lv_obj_t *s5_t_min   = NULL; /* T最小值  (115,2)  */
        static lv_obj_t *s5_t_avg   = NULL; /* T平均值  (115,42) */
        /* Container9 趋势预测: 3个 */
        static lv_obj_t *s5_pred_do  = NULL; /* 溶解氧预测 (20,-27) */
        static lv_obj_t *s5_pred_t   = NULL; /* 水温预测   (20,16) */
        static lv_obj_t *s5_pred_ph  = NULL; /* pH预测     (20,56) */
        /* Container10 存储状态: 4个 */
        static lv_obj_t *s5_emmc   = NULL; /* eMMC路径  (20,-21) */
        static lv_obj_t *s5_saved  = NULL; /* 已存条数  (80,-60) */
        static lv_obj_t *s5_last   = NULL; /* 上次存盘  (20,19)  */
        static lv_obj_t *s5_space  = NULL; /* 空间      (20,58)  */
        /* Container8 趋势: 2个 */
        static lv_obj_t *s5_do_trend  = NULL; /* DO趋势   (25,80)  */
        static lv_obj_t *s5_t_trend   = NULL; /* TEMP趋势 (115,80) */
        static int s5_inited = 0;

        if (!s5_inited) {
            /* Container6 — 实时数据质量 */
            s5_do = lv_label_create(ui_Container6);
            lv_obj_set_width(s5_do, LV_SIZE_CONTENT); lv_obj_set_height(s5_do, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_do, -30); lv_obj_set_y(s5_do, -44);
            lv_obj_set_align(s5_do, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_do, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_temp = lv_label_create(ui_Container6);
            lv_obj_set_width(s5_temp, LV_SIZE_CONTENT); lv_obj_set_height(s5_temp, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_temp, -30); lv_obj_set_y(s5_temp, -12);
            lv_obj_set_align(s5_temp, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_temp, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_ph = lv_label_create(ui_Container6);
            lv_obj_set_width(s5_ph, LV_SIZE_CONTENT); lv_obj_set_height(s5_ph, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_ph, -30); lv_obj_set_y(s5_ph, 17);
            lv_obj_set_align(s5_ph, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_ph, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_turb = lv_label_create(ui_Container6);
            lv_obj_set_width(s5_turb, LV_SIZE_CONTENT); lv_obj_set_height(s5_turb, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_turb, -30); lv_obj_set_y(s5_turb, 51);
            lv_obj_set_align(s5_turb, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_turb, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_stat = lv_label_create(ui_Container6);
            lv_obj_set_width(s5_stat, LV_SIZE_CONTENT); lv_obj_set_height(s5_stat, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_stat, -30); lv_obj_set_y(s5_stat, 88);
            lv_obj_set_align(s5_stat, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_stat, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            /* Container8 — 24h统计 DO列+Temp列 */
            s5_do_max = lv_label_create(ui_Container8);
            lv_obj_set_width(s5_do_max, LV_SIZE_CONTENT); lv_obj_set_height(s5_do_max, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_do_max, 25); lv_obj_set_y(s5_do_max, -38);
            lv_obj_set_align(s5_do_max, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_do_max, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_do_min = lv_label_create(ui_Container8);
            lv_obj_set_width(s5_do_min, LV_SIZE_CONTENT); lv_obj_set_height(s5_do_min, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_do_min, 25); lv_obj_set_y(s5_do_min, 2);
            lv_obj_set_align(s5_do_min, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_do_min, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_do_avg = lv_label_create(ui_Container8);
            lv_obj_set_width(s5_do_avg, LV_SIZE_CONTENT); lv_obj_set_height(s5_do_avg, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_do_avg, 25); lv_obj_set_y(s5_do_avg, 42);
            lv_obj_set_align(s5_do_avg, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_do_avg, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_t_max = lv_label_create(ui_Container8);
            lv_obj_set_width(s5_t_max, LV_SIZE_CONTENT); lv_obj_set_height(s5_t_max, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_t_max, 115); lv_obj_set_y(s5_t_max, -38);
            lv_obj_set_align(s5_t_max, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_t_max, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_t_min = lv_label_create(ui_Container8);
            lv_obj_set_width(s5_t_min, LV_SIZE_CONTENT); lv_obj_set_height(s5_t_min, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_t_min, 115); lv_obj_set_y(s5_t_min, 2);
            lv_obj_set_align(s5_t_min, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_t_min, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_t_avg = lv_label_create(ui_Container8);
            lv_obj_set_width(s5_t_avg, LV_SIZE_CONTENT); lv_obj_set_height(s5_t_avg, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_t_avg, 115); lv_obj_set_y(s5_t_avg, 42);
            lv_obj_set_align(s5_t_avg, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_t_avg, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_do_trend = lv_label_create(ui_Container8);
            lv_obj_set_width(s5_do_trend, LV_SIZE_CONTENT); lv_obj_set_height(s5_do_trend, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_do_trend, 25); lv_obj_set_y(s5_do_trend, 80);
            lv_obj_set_align(s5_do_trend, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_do_trend, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_t_trend = lv_label_create(ui_Container8);
            lv_obj_set_width(s5_t_trend, LV_SIZE_CONTENT); lv_obj_set_height(s5_t_trend, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_t_trend, 115); lv_obj_set_y(s5_t_trend, 80);
            lv_obj_set_align(s5_t_trend, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_t_trend, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            /* Container9 — 趋势预测 */
            s5_pred_do = lv_label_create(ui_Container9);
            lv_obj_set_width(s5_pred_do, LV_SIZE_CONTENT); lv_obj_set_height(s5_pred_do, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_pred_do, 20); lv_obj_set_y(s5_pred_do, -27);
            lv_obj_set_align(s5_pred_do, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_pred_do, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_pred_t = lv_label_create(ui_Container9);
            lv_obj_set_width(s5_pred_t, LV_SIZE_CONTENT); lv_obj_set_height(s5_pred_t, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_pred_t, 20); lv_obj_set_y(s5_pred_t, 16);
            lv_obj_set_align(s5_pred_t, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_pred_t, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_pred_ph = lv_label_create(ui_Container9);
            lv_obj_set_width(s5_pred_ph, LV_SIZE_CONTENT); lv_obj_set_height(s5_pred_ph, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_pred_ph, 20); lv_obj_set_y(s5_pred_ph, 56);
            lv_obj_set_align(s5_pred_ph, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_pred_ph, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            /* Container10 — 存储状态 */
            s5_emmc = lv_label_create(ui_Container10);
            lv_obj_set_width(s5_emmc, LV_SIZE_CONTENT); lv_obj_set_height(s5_emmc, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_emmc, 20); lv_obj_set_y(s5_emmc, -21);
            lv_obj_set_align(s5_emmc, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_emmc, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_saved = lv_label_create(ui_Container10);
            lv_obj_set_width(s5_saved, LV_SIZE_CONTENT); lv_obj_set_height(s5_saved, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_saved, 80); lv_obj_set_y(s5_saved, -60);
            lv_obj_set_align(s5_saved, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_saved, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_last = lv_label_create(ui_Container10);
            lv_obj_set_width(s5_last, LV_SIZE_CONTENT); lv_obj_set_height(s5_last, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_last, 20); lv_obj_set_y(s5_last, 19);
            lv_obj_set_align(s5_last, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_last, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_space = lv_label_create(ui_Container10);
            lv_obj_set_width(s5_space, LV_SIZE_CONTENT); lv_obj_set_height(s5_space, LV_SIZE_CONTENT);
            lv_obj_set_x(s5_space, 20); lv_obj_set_y(s5_space, 58);
            lv_obj_set_align(s5_space, LV_ALIGN_CENTER);
            lv_obj_set_style_text_font(s5_space, &ui_font_chinese28, LV_PART_MAIN | LV_STATE_DEFAULT);

            s5_inited = 1;
        }

        /* === 数据填充 === */

        /* === 数据预处理引擎 === */
        /* 滑动窗口: 10样本, 去重/异常检测 */
        #define WIN 10
        static float hist_do[WIN]  = {0};
        static float hist_t[WIN]   = {0};
        static float hist_ph[WIN]  = {0};
        static float hist_tu[WIN]  = {0};
        static int   hist_idx = 0;
        static int   sample_cnt = 0;
        static float last_do = -99, last_t = -99, last_ph = -99, last_tu = -99;
        int dup_do = 0, dup_t = 0, dup_ph = 0, dup_tu = 0;
        int out_do = 0, out_t = 0, out_ph = 0, out_tu = 0;
        /* ③ 卡死检测：60个周期(30s)内持续不变 → 传感器故障 */
        #define STALL_MAX 60
        static int stall_do = 0, stall_t = 0, stall_ph = 0, stall_tu = 0;
        float sum, avg;

        /* 写入窗口 */
        hist_do[hist_idx] = g_sensor_snapshot.dissolved_o2;
        hist_t[hist_idx]  = g_sensor_snapshot.water_temp;
        hist_ph[hist_idx] = g_sensor_snapshot.ph_value;
        hist_tu[hist_idx] = g_sensor_snapshot.turbidity_ntu;
        hist_idx = (hist_idx + 1) % WIN;
        if (sample_cnt < WIN) sample_cnt++;

        /* 去重检测: 连续相同值 */
        if (fabsf(g_sensor_snapshot.dissolved_o2 - last_do) < 0.01f) dup_do = 1;
        if (fabsf(g_sensor_snapshot.water_temp  - last_t)  < 0.01f) dup_t  = 1;
        if (fabsf(g_sensor_snapshot.ph_value     - last_ph) < 0.01f) dup_ph = 1;
        if (fabsf(g_sensor_snapshot.turbidity_ntu- last_tu) < 0.01f) dup_tu = 1;
        last_do = g_sensor_snapshot.dissolved_o2;
        last_t  = g_sensor_snapshot.water_temp;
        last_ph = g_sensor_snapshot.ph_value;
        last_tu = g_sensor_snapshot.turbidity_ntu;

        /* 异常检测: 超出物理范围 */
        if (g_sensor_snapshot.dissolved_o2 < 0.1f || g_sensor_snapshot.dissolved_o2 > 20.0f) out_do = 1;
        if (g_sensor_snapshot.water_temp   < 0.0f || g_sensor_snapshot.water_temp   > 50.0f) out_t  = 1;
        if (g_sensor_snapshot.ph_value     < 0.0f || g_sensor_snapshot.ph_value     > 14.0f) out_ph = 1;
        if (g_sensor_snapshot.turbidity_ntu< 0.0f || g_sensor_snapshot.turbidity_ntu> 200.0f)out_tu = 1;

        /* ③ 卡死检测: 60周期(30s)持续不变 → 传感器故障 */
        stall_do = dup_do ? stall_do + 1 : 0;
        stall_t  = dup_t  ? stall_t  + 1 : 0;
        stall_ph = dup_ph ? stall_ph + 1 : 0;
        stall_tu = dup_tu ? stall_tu + 1 : 0;

        /* -- 实时数据质量 (Container6) -- */
        sum=0; for(int i=0;i<sample_cnt;i++) sum+=hist_do[i];
        avg = sum/sample_cnt;
        snprintf(buf, sizeof(buf), "%.1f", g_sensor_snapshot.dissolved_o2);
        if (out_do) strcat(buf, " ERR");
        else if (stall_do >= STALL_MAX) strcat(buf, " STALL");
        else if (dup_do) strcat(buf, " DUP");
        else if (g_sensor_snapshot.dissolved_o2 < g_thresholds.do_low_threshold) strcat(buf, " LOW");
        else strcat(buf, " OK");
        lv_label_set_text(s5_do, buf);

        sum=0; for(int i=0;i<sample_cnt;i++) sum+=hist_t[i];
        avg = sum/sample_cnt;
        snprintf(buf, sizeof(buf), "%.1f", g_sensor_snapshot.water_temp);
        if (out_t) strcat(buf, " ERR");
        else if (stall_t >= STALL_MAX) strcat(buf, " STALL");
        else if (dup_t) strcat(buf, " DUP");
        else if (g_sensor_snapshot.water_temp > g_thresholds.temp_high_threshold) strcat(buf, " HIGH");
        else if (g_sensor_snapshot.water_temp < g_thresholds.temp_low_threshold) strcat(buf, " LOW");
        else strcat(buf, " OK");
        lv_label_set_text(s5_temp, buf);

        sum=0; for(int i=0;i<sample_cnt;i++) sum+=hist_ph[i];
        avg = sum/sample_cnt;
        snprintf(buf, sizeof(buf), "%.1f", g_sensor_snapshot.ph_value);
        if (out_ph) strcat(buf, " ERR");
        else if (stall_ph >= STALL_MAX) strcat(buf, " STALL");
        else if (dup_ph) strcat(buf, " DUP");
        else if (g_sensor_snapshot.ph_value < 6.5f) strcat(buf, " LOW");
        else if (g_sensor_snapshot.ph_value > 8.5f) strcat(buf, " HIGH");
        else strcat(buf, " OK");
        lv_label_set_text(s5_ph, buf);

        sum=0; for(int i=0;i<sample_cnt;i++) sum+=hist_tu[i];
        avg = sum/sample_cnt;
        snprintf(buf, sizeof(buf), "%.0f", g_sensor_snapshot.turbidity_ntu);
        if (out_tu) strcat(buf, " ERR");
        else if (stall_tu >= STALL_MAX) strcat(buf, " STALL");
        else if (dup_tu) strcat(buf, " DUP");
        else if (g_sensor_snapshot.turbidity_ntu > 50.0f) strcat(buf, " HIGH");
        else strcat(buf, " OK");
        lv_label_set_text(s5_turb, buf);

        /* 状态: 传感器有效性 + 数据质量统计 */
        {
            int errs = out_do+out_t+out_ph+out_tu;
            int dups = dup_do+dup_t+dup_ph+dup_tu;
            int stalls = (stall_do>=STALL_MAX)+(stall_t>=STALL_MAX)+(stall_ph>=STALL_MAX)+(stall_tu>=STALL_MAX);
            if (!g_sensor_snapshot.do_valid || !g_sensor_snapshot.temp_valid || !g_sensor_snapshot.ph_valid)
                snprintf(buf, sizeof(buf), "SENSOR ERR");
            else if (errs > 0)
                snprintf(buf, sizeof(buf), "FILTER:%d", errs);
            else if (stalls > 0)
                snprintf(buf, sizeof(buf), "STALL:%d OK", stalls);
            else if (dups > 0)
                snprintf(buf, sizeof(buf), "DUP:%d OK", dups);
            else
                snprintf(buf, sizeof(buf), "ALL OK");
            lv_label_set_text(s5_stat, buf);
        }

        /* -- 24h统计 (Container8) — 运行极值+平均 -- */
        {
            static float _do_max = -999, _do_min = 999, _do_sum = 0;
            static float _t_max  = -999, _t_min  = 999, _t_sum  = 0;
            static int   _cnt = 0;
            float dv = g_sensor_snapshot.dissolved_o2;
            float tv = g_sensor_snapshot.water_temp;
            _cnt++;
            if (dv > _do_max) _do_max = dv;
            if (dv < _do_min) _do_min = dv;
            if (tv > _t_max)  _t_max  = tv;
            if (tv < _t_min)  _t_min  = tv;
            _do_sum += dv; _t_sum += tv;

            snprintf(buf, sizeof(buf), "%.1f", _do_max);
            lv_label_set_text(s5_do_max, buf);
            snprintf(buf, sizeof(buf), "%.1f", _do_min);
            lv_label_set_text(s5_do_min, buf);
            snprintf(buf, sizeof(buf), "%.1f", _do_sum / _cnt);
            lv_label_set_text(s5_do_avg, buf);

            snprintf(buf, sizeof(buf), "%.1f", _t_max);
            lv_label_set_text(s5_t_max, buf);
            snprintf(buf, sizeof(buf), "%.1f", _t_min);
            lv_label_set_text(s5_t_min, buf);
            snprintf(buf, sizeof(buf), "%.1f", _t_sum / _cnt);
            lv_label_set_text(s5_t_avg, buf);
        }

        lv_label_set_text(s5_do_trend, g_sensor_snapshot.dissolved_o2 > g_thresholds.do_target ? "+ UP" : "- DN");
        lv_label_set_text(s5_t_trend,  g_sensor_snapshot.water_temp  > g_thresholds.temp_target ? "+ UP" : "- DN");

        /* -- 趋势预测 (Container9) — 线性回归 -- */
        {
            /* 用10样本滑动窗口做最小二乘线性拟合
               预测公式: y = intercept + slope * x
               x: 0(WIN-1旧) → WIN-1(最新), 预测 x=WIN 的下一个值 */
            #define WIN 10
            const float sx  = 45.0f;    /* Σx   = 0+1+...+9 */
            const float sxx = 285.0f;   /* Σx²  = 0²+1²+...+9² */
            const float denom = WIN * sxx - sx * sx;  /* = 825 */

            float do_sy=0, do_sxy=0, t_sy=0, t_sxy=0, ph_sy=0, ph_sxy=0;
            for (int i = 0; i < WIN; i++) {
                float dv = hist_do[(hist_idx + i) % WIN];
                float tv = hist_t[(hist_idx + i) % WIN];
                float pv = hist_ph[(hist_idx + i) % WIN];
                float x = (float)i;
                do_sy  += dv;  do_sxy += x * dv;
                t_sy   += tv;  t_sxy  += x * tv;
                ph_sy  += pv;  ph_sxy += x * pv;
            }
            float do_slope = (WIN * do_sxy - sx * do_sy) / denom;
            float t_slope  = (WIN * t_sxy  - sx * t_sy)  / denom;
            float ph_slope = (WIN * ph_sxy - sx * ph_sy) / denom;

            /* 预测下 10 个周期 (5秒) 后的值 */
            float do_pred = (do_sy - do_slope * sx) / WIN + do_slope * (WIN + 10);
            float t_pred  = (t_sy  - t_slope  * sx) / WIN + t_slope  * (WIN + 10);
            float ph_pred = (ph_sy - ph_slope * sx) / WIN + ph_slope * (WIN + 10);

            snprintf(buf, sizeof(buf), "%.1f mg/L %s", do_pred,
                do_slope > 0.02f ? "UP" : do_slope < -0.02f ? "DN" : "-");
            lv_label_set_text(s5_pred_do, buf);

            snprintf(buf, sizeof(buf), "%.1f C %s", t_pred,
                t_slope > 0.02f ? "UP" : t_slope < -0.02f ? "DN" : "-");
            lv_label_set_text(s5_pred_t, buf);

            snprintf(buf, sizeof(buf), "%.2f %s", ph_pred,
                ph_slope > 0.01f ? "UP" : ph_slope < -0.01f ? "DN" : "-");
            lv_label_set_text(s5_pred_ph, buf);
        }

        /* -- 存储状态 (Container10) -- */
        lv_label_set_text(s5_emmc,  "./sensor.log");
        {
            struct stat st;
            if (stat("./sensor.log", &st) == 0) {
                snprintf(buf, sizeof(buf), "%ld", (long)(st.st_size / 64));
                lv_label_set_text(s5_saved, buf);
                strftime(buf, sizeof(buf), "%H:%M", localtime(&st.st_mtime));
                lv_label_set_text(s5_last, buf);
            } else {
                lv_label_set_text(s5_saved, "0");
                lv_label_set_text(s5_last,  "--:--");
            }
        }
        {
            struct statvfs vfs;
            if (statvfs("./sensor.log", &vfs) == 0) {
                unsigned long long free_bytes =
                    (unsigned long long)vfs.f_frsize * vfs.f_bavail;
                if (free_bytes >= 1073741824ULL)
                    snprintf(buf, sizeof(buf), "%.1fG", free_bytes / 1073741824.0);
                else if (free_bytes >= 1048576ULL)
                    snprintf(buf, sizeof(buf), "%.0fM", free_bytes / 1048576.0);
                else
                    snprintf(buf, sizeof(buf), "%.0fK", free_bytes / 1024.0);
                lv_label_set_text(s5_space, buf);
            } else {
                lv_label_set_text(s5_space, "--");
            }
        }
    }

    /* ========== Screen4：AI 响应显示 + 传感器数据 ========== */
    if (active == ui_Screen4 && ui_Screen4 != NULL) {
        /* 首次激活 → 加载 FreeType（只试一次） */
        static int ai_font_tried = 0;
        static lv_font_t *ai_font = NULL;
        if (!ai_font_tried && ui_Label56 != NULL) {
            ai_font_tried = 1;
            ai_font = lv_freetype_font_create("/simkai.ttf",
                         LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 22,
                         LV_FREETYPE_FONT_STYLE_NORMAL);
            if (ai_font) {
                lv_obj_set_style_text_font(ui_Label56, ai_font, 0);
                printf("[AI] FreeType 字体加载成功\n");
            }
        }

        /* AI 响应 */
        if (g_ai_ready != 0 && ui_Label56 != NULL) {
            lv_label_set_text(ui_Label56, g_ai_response);
            if (g_ai_ready == 1) {
                printf("[AI] 回复已显示: %.80s...\n", g_ai_response);
            }
            g_ai_ready = 0;
        }

        /* 传感器实时数据 */
        if (ui_Label9v != NULL) {
            snprintf(buf, sizeof(buf), "%.2fmg", g_sensor_snapshot.dissolved_o2);
            lv_label_set_text(ui_Label9v, buf);
        }
        if (ui_Label52v != NULL) {
            snprintf(buf, sizeof(buf), "%.1f℃", g_sensor_snapshot.water_temp);
            lv_label_set_text(ui_Label52v, buf);
        }
        if (ui_Label54v != NULL) {
            snprintf(buf, sizeof(buf), "%.2f", g_sensor_snapshot.ph_value);
            lv_label_set_text(ui_Label54v, buf);
        }
        if (ui_Label55v != NULL) {
            snprintf(buf, sizeof(buf), "%.1fNT", g_sensor_snapshot.turbidity_ntu);
            lv_label_set_text(ui_Label55v, buf);
        }
    }

    /* 屏显数据 — 每60秒一条 */
    if (call_count % 120 == 0) {
        printf("[SYS] DO=%.1f(%.1f) T=%.1f(%.1f) | 增氧%.0f%% 泵%.0f%% 投喂%d\n",
               g_sensor_snapshot.dissolved_o2, g_thresholds.do_target,
               g_sensor_snapshot.water_temp,  g_thresholds.temp_target,
               g_device_state[DEVICE_AERATOR].duty_cycle,
               g_device_state[DEVICE_WATER_PUMP].duty_cycle,
               g_device_state[DEVICE_FEEDER].running);
    }
}

// ==================== STM32 传感器串口模块 ====================

#define SENSOR_UART_DEV  "/dev/ttySAC2"
#define SENSOR_UART_BAUD B115200

static int sensor_uart_fd = -1;

/* ---- UART 初始化 ---- */
static int sensor_uart_init(int fd)
{
    struct termios opt;
    tcgetattr(fd, &opt);
    cfmakeraw(&opt);
    opt.c_cflag |= CLOCAL | CREAD;
    cfsetospeed(&opt, SENSOR_UART_BAUD);
    cfsetispeed(&opt, SENSOR_UART_BAUD);
    opt.c_cflag &= ~CSIZE;
    opt.c_cflag |= CS8;
    opt.c_cflag &= ~PARENB;
    opt.c_cflag &= ~CSTOPB;
    opt.c_cc[VTIME] = 1;
    opt.c_cc[VMIN]  = 0;
    tcflush(fd, TCIOFLUSH);
    tcsetattr(fd, TCSANOW, &opt);
    return 0;
}

/*
 * 解析 STM32 发来的 JSON：
 *   {"temp":29.7,"humi":67.4,"light":1091.5,"do":8.11,"ph":7.90,"turb":42.6,"stat":7}
 *
 * 映射关系:
 *   "do"   → dissolved_o2  → label59 (溶解氧实时)
 *   "temp" → water_temp    → label80 (水温实时)
 *   "ph"   → ph_value
 *   "humi" → air_humidity
 *   "light"→ light_lux
 *   "turb" → turbidity_ntu
 *   "stat" → 传感器状态位
 */
static int parse_sensor_json(const char *json, SensorSnapshot *snap)
{
    float t, h, l, d, p, u;
    int s;

    int n = sscanf(json,
        "{\"temp\":%f,\"humi\":%f,\"light\":%f,\"do\":%f,\"ph\":%f,\"turb\":%f,\"stat\":%d}",
        &t, &h, &l, &d, &p, &u, &s);

    if (n < 6) {
        printf("[SENSOR] JSON parse fail, got %d fields\n", n);
        return 0;
    }

    snap->water_temp    = t;
    snap->air_temp      = t;
    snap->air_humidity  = h;
    snap->light_lux     = l;
    snap->dissolved_o2  = d;
    snap->ph_value      = p;
    snap->turbidity_ntu = u;
    snap->timestamp_ms  = 0;

    /* stat 位: bit0=temp bit1=humi bit2=light bit3=do bit4=ph bit5=turb */
    snap->temp_valid = (s & 0x01) ? 1 : 0;
    snap->do_valid   = (s & 0x08) ? 1 : 0;
    snap->ph_valid   = (s & 0x10) ? 1 : 0;

    /* 第一次收到真实数据 → 停止模拟 */
    if (!g_sensor_connected) {
        g_sensor_connected = 1;
        printf("[SENSOR] 真实数据已连接\n");
    }

    return 1;
}

/* ---- 串口接收线程 ---- */
static void *sensor_recv_thread(void *arg)
{
    (void)arg;

    char buf[256];
    int  pos = 0;

    while (1) {
        char ch;
        int len = read(sensor_uart_fd, &ch, 1);
        if (len <= 0) {
            usleep(10000);  // 10ms
            continue;
        }

        if (ch == '\n' || ch == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';

                SensorSnapshot snap;
                if (parse_sensor_json(buf, &snap)) {
                    AutoControl_UpdateSensors(&snap);
                }

                pos = 0;
            }
        } else if (pos < (int)sizeof(buf) - 1) {
            buf[pos++] = ch;
        }
    }
    return NULL;
}

/* ---- 外部调用入口 ---- */

void SENSOR_UART_INIT(void)
{
    if (sensor_uart_fd >= 0) return;

    sensor_uart_fd = open(SENSOR_UART_DEV, O_RDWR | O_NOCTTY);
    if (sensor_uart_fd == -1) {
        printf("[SENSOR] ⚠ 无法打开 %s, 将使用模拟数据\n", SENSOR_UART_DEV);
        printf("[SENSOR] 连接 STM32 后会自动切换到真实传感器\n");
        return;
    }

    sensor_uart_init(sensor_uart_fd);
    printf("[SENSOR] STM32 UART 已打开: %s, baud=%d (等待数据...)\n",
           SENSOR_UART_DEV, SENSOR_UART_BAUD);

    pthread_t tid;
    pthread_create(&tid, NULL, sensor_recv_thread, NULL);
    pthread_detach(tid);
}
