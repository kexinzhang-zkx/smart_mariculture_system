#include "../ui.h"

/* 6 个实时数据标签 */
lv_obj_t *ui_SensorTemp;
lv_obj_t *ui_SensorHumi;
lv_obj_t *ui_SensorLight;
lv_obj_t *ui_SensorDO;
lv_obj_t *ui_SensorPH;
lv_obj_t *ui_SensorTurb;

/* 创建单个传感器卡片 */
static lv_obj_t *create_sensor_card(lv_obj_t *parent, const char *title,
    const char *img_src, const char *unit, lv_obj_t **val_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 105, 130);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(card, 200, 0);
    lv_obj_set_style_pad_all(card, 5, 0);

    /* 标题 */
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_center(label);
    lv_obj_set_y(label, -38);
    lv_obj_set_style_text_color(label, lv_color_hex(0x008BFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);

    /* 图标 */
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, img_src);
    lv_obj_set_size(img, 35, 35);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -5);

    /* 数值 */
    *val_label = lv_label_create(card);
    lv_label_set_text(*val_label, "--");
    lv_obj_align(*val_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_font(*val_label, &lv_font_montserrat_14, 0);

    return card;
}

/* 创建传感器行 (3个卡片一行) */
static lv_obj_t *create_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(95), 140);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

void ui_Screen4_screen_init(void)
{
    ui_Screen4 = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_Screen4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen4, lv_color_hex(0x003366), 0);

    /* 主容器 */
    ui_Container30 = lv_obj_create(ui_Screen4);
    lv_obj_remove_style_all(ui_Container30);
    lv_obj_set_size(ui_Container30, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(ui_Container30, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_Container30, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ui_Container30, 8, 0);

    /* 标题 */
    ui_Label16 = lv_label_create(ui_Container30);
    lv_label_set_text(ui_Label16, "海洋牧场实时监测");
    lv_obj_set_style_text_color(ui_Label16, lv_color_hex(0x00D4FF), 0);
    lv_obj_set_style_text_font(ui_Label16, &ui_font_Font1, 0);

    /* 数据卡片容器 */
    ui_Container35 = lv_obj_create(ui_Container30);
    lv_obj_remove_style_all(ui_Container35);
    lv_obj_set_size(ui_Container35, lv_pct(95), 340);
    lv_obj_set_flex_flow(ui_Container35, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_Container35, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(ui_Container35, 16, 0);
    lv_obj_set_style_bg_color(ui_Container35, lv_color_hex(0x004488), 0);
    lv_obj_set_style_bg_opa(ui_Container35, 150, 0);
    lv_obj_set_style_border_color(ui_Container35, lv_color_hex(0x0088CC), 0);
    lv_obj_set_style_border_opa(ui_Container35, 200, 0);
    lv_obj_set_style_border_width(ui_Container35, 2, 0);

    /* 行1: 温度 | 湿度 | 光照 */
    lv_obj_t *row1 = create_row(ui_Container35);
    create_sensor_card(row1, "温度", &ui_img_1554780076, "℃", &ui_SensorTemp);
    create_sensor_card(row1, "湿度", &ui_img_147463449, "%", &ui_SensorHumi);
    create_sensor_card(row1, "光照", &ui_img_1138793207, "lux", &ui_SensorLight);

    /* 行2: DO | pH | 浊度 */
    lv_obj_t *row2 = create_row(ui_Container35);
    create_sensor_card(row2, "溶解氧", &ui_img_co2_png, "mg/L", &ui_SensorDO);
    create_sensor_card(row2, "pH 值", &ui_img_2_png, "", &ui_SensorPH);
    create_sensor_card(row2, "浊度", &ui_img_1138793207, "NTU", &ui_SensorTurb);

    /* 按钮栏 */
    ui_Container29 = lv_obj_create(ui_Container30);
    lv_obj_remove_style_all(ui_Container29);
    lv_obj_set_size(ui_Container29, lv_pct(85), 55);
    lv_obj_set_flex_flow(ui_Container29, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_Container29, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui_Button7 = lv_button_create(ui_Container29);
    lv_obj_set_size(ui_Button7, 100, 45);
    lv_obj_set_style_bg_color(ui_Button7, lv_color_hex(0x008BFF), 0);
    ui_Label23 = lv_label_create(ui_Button7);
    lv_label_set_text(ui_Label23, "返回");
    lv_obj_set_style_text_font(ui_Label23, &ui_font_Font1, 0);
    lv_obj_center(ui_Label23);

    ui_Button9 = lv_button_create(ui_Container29);
    lv_obj_set_size(ui_Button9, 140, 45);
    lv_obj_set_style_bg_color(ui_Button9, lv_color_hex(0x008BFF), 0);
    ui_Label29 = lv_label_create(ui_Button9);
    lv_label_set_text(ui_Label29, "视频播放");
    lv_obj_set_style_text_font(ui_Label29, &ui_font_Font1, 0);
    lv_obj_center(ui_Label29);

    ui_Button5 = lv_button_create(ui_Container29);
    lv_obj_set_size(ui_Button5, 140, 45);
    lv_obj_set_style_bg_color(ui_Button5, lv_color_hex(0x008BFF), 0);
    ui_Label21 = lv_label_create(ui_Button5);
    lv_label_set_text(ui_Label21, "控制中心");
    lv_obj_set_style_text_font(ui_Label21, &ui_font_Font1, 0);
    lv_obj_center(ui_Label21);

    lv_obj_add_event_cb(ui_Button7, ui_event_Button7, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Button9, ui_event_Button9, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Button5, ui_event_Button5, LV_EVENT_CLICKED, NULL);
}
