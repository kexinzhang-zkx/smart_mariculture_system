import re
f = '/home/huangsimin09/lv_port_linux_sdl_gec6818/ui/ui_events.c'
with open(f, 'r', errors='replace') as fp:
    c = fp.read()
c = re.sub(r'.*if\(strcmp\(acc,admin\).*\n?', '', c)
c = re.sub(r'.*static int admin_logged_in.*\n?', '', c)
c = c.replace('    // 遍历查找用户',
    '    if(strcmp(acc,"admin")==0 && strcmp(pwd,"123456")==0) {\n'
    '        show_tips_popup("Admin login OK");\n'
    '        lv_timer_t *jt = lv_timer_create(page_jump_timer, 2000, (void *)JUMP_TO_MAIN);\n'
    '        lv_timer_set_repeat_count(jt, 1);\n'
    '        return;\n'
    '    }\n'
    '    // 遍历查找用户')
with open(f, 'w') as fp:
    fp.write(c)
print('OK')
