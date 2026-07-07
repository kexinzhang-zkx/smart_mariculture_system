import os, glob
from PIL import Image

png_dir = r"D:\粤嵌实训\code\STM32\STM32发布数据到MQTT完整代码V1"
out_dir = r"D:\粤嵌实训\code\STM32\STM32发布数据到MQTT完整代码V1\build\images"
os.makedirs(out_dir, exist_ok=True)

pngs = [
    ("温度 (1).png", "ui_img_sensor_temp"),
    ("湿度.png", "ui_img_sensor_humi"),
    ("光照.png", "ui_img_sensor_light"),
    ("PH值.png", "ui_img_sensor_ph"),
    ("溶解氧.png", "ui_img_sensor_do"),
    ("浊度.png", "ui_img_sensor_turb"),
]

for filename, var_name in pngs:
    path = os.path.join(png_dir, filename)
    if not os.path.exists(path):
        print(f"NOT FOUND: {path}")
        continue

    img = Image.open(path).convert("RGBA")
    w, h = img.size
    data = img.tobytes()

    # Generate C file
    c_path = os.path.join(out_dir, var_name + ".c")
    with open(c_path, "w") as f:
        f.write("// Auto-generated sensor icon\n")
        f.write('#include "../../../lvgl/lvgl.h"\n\n')
        f.write(f"const uint8_t {var_name}_data[] = {{\n    ")
        for i, b in enumerate(data):
            f.write(f"0x{b:02X}, ")
            if (i + 1) % 16 == 0:
                f.write("\n    ")
        f.write("\n};\n\n")
        f.write(f"const lv_image_dsc_t {var_name} = {{\n")
        f.write(f"    .header.w = {w},\n")
        f.write(f"    .header.h = {h},\n")
        f.write(f"    .data_size = sizeof({var_name}_data),\n")
        f.write(f"    .header.cf = LV_COLOR_FORMAT_NATIVE_WITH_ALPHA,\n")
        f.write(f"    .header.magic = LV_IMAGE_HEADER_MAGIC,\n")
        f.write(f"    .data = {var_name}_data\n")
        f.write("};\n")
    print(f"Generated: {var_name}.c ({w}x{h}, {len(data)} bytes)")

# Print declares for ui.h
print("\n// Add to ui.h:")
for _, var_name in pngs:
    print(f"LV_IMG_DECLARE({var_name});")

# Print filelist entries
print("\n// Add to filelist.txt:")
for _, var_name in pngs:
    print(f"ui/images/{var_name}.c")
