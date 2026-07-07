import paramiko, time, base64

ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect("192.168.10.100", username="root", password="1", timeout=10)
print("Connected!")

ssh.exec_command("killall main 2>/dev/null")
time.sleep(1)

# Read binary and encode
with open(r"d:\粤嵌实训\code\STM32\STM32发布数据到MQTT完整代码V1\build\main", "rb") as f:
    data_b64 = base64.b64encode(f.read()).decode()

print(f"Uploading {len(data_b64)} bytes base64...")

# Split and write in chunks
chunk_size = 100000
stdin, stdout, stderr = ssh.exec_command(f"rm -f /main; cat > /main.b64")
for i in range(0, len(data_b64), chunk_size):
    stdin.write(data_b64[i:i+chunk_size])
stdin.close()
stdout.read()

stdin, stdout, stderr = ssh.exec_command("base64 -d /main.b64 > /main; chmod 755 /main; rm /main.b64")
stdout.read()
stderr.read()
print("Uploaded!")

time.sleep(1)
stdin, stdout, stderr = ssh.exec_command("cd / && ./main &")
time.sleep(2)

stdin, stdout, stderr = ssh.exec_command("pidof main")
print("PID:", stdout.read().decode().strip())
ssh.close()
