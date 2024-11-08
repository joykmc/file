import os

# 设置要读取的偏移位置和长度（字节数）
offset = 128  # 偏移位置，例如从第 128 个字节开始
fixed_length = 64  # 要读取的字节长度

# 循环从 eth1 到 eth128
for i in range(1, 129):
    present_path = f"/sys_switch/transceiver/eth{i}/present"
    eeprom_path = f"/sys_switch/transceiver/eth{i}/eeprom"
    
    # 检查并读取 present 文件
    if os.path.exists(present_path):
        try:
            with open(present_path, 'r') as file:
                present_content = file.read().strip()
                print(f"内容 ({present_path}): {present_content}")
        except IOError as e:
            print(f"无法读取文件 {present_path}: {e}")
    else:
        print(f"文件 {present_path} 不存在")
    
    if present_content == "0":
        continue
    # 检查并读取 eeprom 文件的固定长度内容
    if os.path.exists(eeprom_path):
        try:
            with open(eeprom_path, 'rb') as file:
                # 将读取位置设置到偏移处
                file.seek(offset)
                eeprom_content = file.read(fixed_length)
                # 将读取的二进制内容转换为十六进制字符串或其他可读格式
                hex_content = eeprom_content.hex()
                print(f"内容 ({eeprom_path}, 从偏移 {offset} 读取 {fixed_length} 字节): {hex_content}")
        except IOError as e:
            print(f"无法读取文件 {eeprom_path}: {e}")
    else:
        print(f"文件 {eeprom_path} 不存在")
