#include "../ui.h"
#include <string.h>

extern lv_style_t style_chinese;

lv_obj_t *ui_SensorTemp;
lv_obj_t *ui_SensorHumi;
lv_obj_t *ui_SensorLight;
lv_obj_t *ui_SensorDO;
lv_obj_t *ui_SensorPH;
lv_obj_t *ui_SensorTurb;

static lv_obj_t *create_card(lv_obj_t *parent, const char *title,
    const void *img_src, lv_obj_t **val_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 105, 130);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(card, 200, 0);
    lv_obj_set_style_pad_all(card, 5, 0);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_center(label);
    lv_obj_set_y(label, -38);
    lv_obj_set_style_text_color(label, lv_color_hex(0x008BFF), 0);
    lv_obj_add_style(label, &style_chinese, 0);

    if (img_path) {
        lv_obj_t *img = lv_image_create(card);
        lv_image_set_src(img, img_path);
        lv_obj_set_size(img, 35, 35);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, -5);
    }

    *val_label = lv_label_create(card);
    lv_label_set_text(*val_label, "--");
    lv_obj_align(*val_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_font(*val_label, &lv_font_montserrat_14, 0);
    return card;
}

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
    lv_obj_set_style_bg_image_src(ui_Screen4, &ui_img_711078256, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 主容器 — 和其他页面布局一致 */
    ui_Container30 = lv_obj_create(ui_Screen4);
    lv_obj_remove_style_all(ui_Container30);
    lv_obj_set_size(ui_Container30, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(ui_Container30, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_Container30, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 标题 — 和 Screen3 同风格: Font1, 白底蓝影 */
    ui_Label16 = lv_label_create(ui_Container30);
    lv_label_set_text(ui_Label16, "物联网海洋牧场终端");
    lv_obj_set_style_text_color(ui_Label16, lv_color_hex(0x008BFF), 0);
    lv_obj_set_style_text_opa(ui_Label16, 255, 0);
    lv_obj_set_style_text_font(ui_Label16, &ui_font_Font1, 0);
    lv_obj_set_style_radius(ui_Label16, 8, 0);
    lv_obj_set_style_bg_color(ui_Label16, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ui_Label16, 255, 0);
    lv_obj_set_style_shadow_color(ui_Label16, lv_color_hex(0x008BFF), 0);
    lv_obj_set_style_shadow_opa(ui_Label16, 255, 0);
    lv_obj_set_style_shadow_width(ui_Label16, 20, 0);
    lv_obj_set_style_shadow_spread(ui_Label16, 1, 0);
    lv_obj_set_style_shadow_offset_x(ui_Label16, 6, 0);
    lv_obj_set_style_shadow_offset_y(ui_Label16, 6, 0);

    /* 数据卡片 */
    ui_Container35 = lv_obj_create(ui_Container30);
    lv_obj_remove_style_all(ui_Container35);
    lv_obj_set_size(ui_Container35, lv_pct(95), 340);
    lv_obj_set_flex_flow(ui_Container35, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_Container35, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(ui_Container35, 16, 0);
    lv_obj_set_style_bg_color(ui_Container35, lv_color_hex(0x004488), 0);
    lv_obj_set_style_bg_opa(ui_Container35, 150, 0);

    lv_obj_t *row1 = create_row(ui_Container35);
    create_card(row1, "温度", "/温度 (1).png", &ui_SensorTemp);
    create_card(row1, "湿度", "/湿度.png", &ui_SensorHumi);
    create_card(row1, "光照", "/光照.png", &ui_SensorLight);

    lv_obj_t *row2 = create_row(ui_Container35);
    create_card(row2, "溶解氧", "/溶解氧.png", &ui_SensorDO);
    create_card(row2, "pH 值", "/PH值.png", &ui_SensorPH);
    create_card(row2, "浊度", "/浊度.png", &ui_SensorTurb);

    /* 按钮 — 和其他页面字体一致: ui_font_Font1 */
    ui_Container29 = lv_obj_create(ui_Container30);
    lv_obj_remove_style_all(ui_Container29);
    lv_obj_set_size(ui_Container29, lv_pct(85), 55);
    lv_obj_set_flex_flow(ui_Container29, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_Container29, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui_Button7 = lv_button_create(ui_Container29);
    lv_obj_set_size(ui_Button7, 100, 50);
    lv_obj_set_style_bg_color(ui_Button7, lv_color_hex(0x008BFF), 0);
    ui_Label23 = lv_label_create(ui_Button7);
    lv_label_set_text(ui_Label23, "返回");
    lv_obj_set_style_text_font(ui_Label23, &ui_font_Font1, 0);
    lv_obj_center(ui_Label23);

    ui_Button9 = lv_button_create(ui_Container29);
    lv_obj_set_size(ui_Button9, 140, 50);
    lv_obj_set_style_bg_color(ui_Button9, lv_color_hex(0x008BFF), 0);
    ui_Label29 = lv_label_create(ui_Button9);
    lv_label_set_text(ui_Label29, "视频播放");
    lv_obj_set_style_text_font(ui_Label29, &ui_font_Font1, 0);
    lv_obj_center(ui_Label29);

    ui_Button5 = lv_button_create(ui_Container29);
    lv_obj_set_size(ui_Button5, 140, 50);
    lv_obj_set_style_bg_color(ui_Button5, lv_color_hex(0x008BFF), 0);
    ui_Label21 = lv_label_create(ui_Button5);
    lv_label_set_text(ui_Label21, "控制中心");
    lv_obj_set_style_text_font(ui_Label21, &ui_font_Font1, 0);
    lv_obj_center(ui_Label21);

    lv_obj_add_event_cb(ui_Button7, ui_event_Button7, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Button9, ui_event_Button9, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Button5, ui_event_Button5, LV_EVENT_CLICKED, NULL);
}
