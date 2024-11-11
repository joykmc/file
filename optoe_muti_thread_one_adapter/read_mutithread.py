import os
import threading

# 设置要读取的偏移位置和长度（字节数）
offset = 300  # 偏移位置，例如从第 128 个字节开始
fixed_length = 1  # 要读取的字节长度

# 读取 eeprom 文件的函数
def read_eeprom(path, offset, fixed_length):
    if os.path.exists(path):
        try:
            with open(path, 'rb', buffering=0) as file:
                file.seek(offset)
                content = file.read(fixed_length)
                hex_content = content.hex()
                print(f"内容 ({path}, 从偏移 {offset} 读取 {fixed_length} 字节): {hex_content}")
        except IOError as e:
            print(f"无法读取文件 {path}: {e}")
    else:
        print(f"文件 {path} 不存在")

# 创建线程并读取每个 eeprom
threads = []
for i in range(1, 129):
    present_path = f"/sys_switch/transceiver/eth{i}/present"
    #eeprom_path = f"/sys_switch/transceiver/eth{i}/eeprom"

    # �~@�~_�并读�~O~V present �~V~G件
    #if os.path.exists(present_path):
    #    try:
    #        with open(present_path, 'r') as file:
    #            present_content = file.read().strip()
    #            print(f"�~F~E容 ({present_path}): {present_content}")
    #    except IOError as e:
    #        print(f"�~W| �~U读�~O~V�~V~G件 {present_path}: {e}")
    #else:
    #    print(f"�~V~G件 {present_path} �~M�~X�~\�")

    #if present_content == "0":
    #    continue

    #eeprom_path = f"/sys_switch/transceiver/eth{i}/eeprom"
    eeprom_path = "/sys_switch/transceiver/eth6/eeprom"
    thread = threading.Thread(target=read_eeprom, args=(eeprom_path, offset, fixed_length))
    threads.append(thread)
    thread.start()

# 等待所有线程完成
for thread in threads:
    thread.join()

print("所有 eeprom 文件读取完成。")
