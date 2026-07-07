import re, os

f = '/home/huangsimin09/lv_port_linux_sdl_gec6818/ui/ui_events.c'
with open(f, 'r', errors='replace') as fp:
    c = fp.read()

# Add extern style_chinese after include block
c = c.replace('#include "ui.h"',
    '#include "ui.h"\nextern lv_style_t style_chinese;')

# Fix show_tips_popup - add Chinese style
old_popup = 'lv_obj_t *label = lv_label_create(popup);'
new_popup = 'lv_obj_t *label = lv_label_create(popup);\n    lv_obj_add_style(label, &style_chinese, 0);'
c = c.replace(old_popup, new_popup)

# Fix popup texts to Chinese (only the ones I added)
c = c.replace('"Admin login OK"', '"管理员登录成功"')
c = c.replace('"Admin login OK"', '"管理员登录成功"')

# Also update existing popup texts to Chinese
texts = [
    ('"账号或密码不能为空"', '"账号或密码不能为空"'),  # already Chinese
    ('"登录成功"', '"登录成功"'),
    ('"注册成功"', '"注册成功"'),
    ('"信息不能为空"', '"信息不能为空"'),
    ('"两次密码不一致"', '"两次密码不一致"'),
    ('"用户名已存在"', '"用户名已存在"'),
    ('"账号或密码错误"', '"账号或密码错误"'),
    ('"打开LED"', '"打开LED"'),
    ('"关闭LED"', '"关闭LED"'),
    ('"打开风扇"', '"打开风扇"'),
    ('"关闭风扇"', '"关闭风扇"'),
    ('"打开加热器"', '"打开加热器"'),
    ('"关闭加热器"', '"关闭加热器"'),
    ('"打开百叶窗"', '"打开百叶窗"'),
    ('"关闭百叶窗"', '"关闭百叶窗"'),
    ('"LED控制指令发布失败"', '"LED指令发送失败"'),
]

with open(f, 'w', encoding='utf-8') as fp:
    fp.write(c)
print('ui_events.c fixed!')
