f = r"d:\粤嵌实训\code\STM32\STM32发布数据到MQTT完整代码V1\build\ui_Screen4_wsl.c"
with open(f, encoding='utf-8') as fp:
    c = fp.read()

c = c.replace('if (img_path)', 'if (img_src)')

# Replace the Chinese-named PNG paths with C struct refs
old = ['"/\xe6\xa9\xb1\xe5\xba\xa6 (1).png"', '"/\xe6\xb9\xbf\xe5\xba\xa6.png"',
       '"/\xe5\x85\x89\xe7\x85\xa7.png"', '"/PH\xe5\x80\xbc.png"',
       '"/\xe6\xba\xb6\xe8\xa7\xa3\xe6\xb0\xa7.png"', '"/\xe6\xb5\x8a\xe5\xba\xa6.png"']
new = ['&ui_img_sensor_temp', '&ui_img_sensor_humi', '&ui_img_sensor_light',
       '&ui_img_sensor_ph', '&ui_img_sensor_do', '&ui_img_sensor_turb']

for o, n in zip(old, new):
    if o in c:
        c = c.replace(o, n)
        print(f"Replaced: {n}")
    else:
        print(f"NOT FOUND: searching for alt patterns...")
        # Try to find the line
        for line in c.split('\n'):
            if 'create_card' in line and '.png' in line:
                print(f"  Card line: {line.strip()}")

with open(f, 'w', encoding='utf-8') as fp:
    fp.write(c)
print("Done!")
