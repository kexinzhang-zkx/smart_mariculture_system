#!/bin/bash
FILE=/home/huangsimin09/lv_port_linux_sdl_gec6818/ui/ui_events.c
# Delete any broken admin lines
sed -i '/if(strcmp(acc,admin)/d' "$FILE" 2>/dev/null
sed -i '/static int admin_logged_in/d' "$FILE" 2>/dev/null
# Add proper admin login before "遍历查找用户"
sed -i '/\/\/ 遍历查找用户/i\    if(strcmp(acc,"admin")==0 \&\& strcmp(pwd,"123456")==0) {\
        show_tips_popup("Admin login OK");\
        lv_timer_t *jt = lv_timer_create(page_jump_timer, 2000, (void *)JUMP_TO_MAIN);\
        lv_timer_set_repeat_count(jt, 1);\
        return;\
    }' "$FILE" 2>/dev/null
echo "Fix applied"
