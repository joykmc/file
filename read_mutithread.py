import os
import threading

# 设置要读取的偏移位置和长度（字节数）
offset = 128  # 偏移位置，例如从第 128 个字节开始
fixed_length = 64  # 要读取的字节长度

# 读取 eeprom 文件的函数
def read_eeprom(path, offset, fixed_length):
    if os.path.exists(path):
        try:
            with open(path, 'rb') as file:
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
    eeprom_path = f"/sys_switch/transceiver/eth{i}/eeprom"
    thread = threading.Thread(target=read_eeprom, args=(eeprom_path, offset, fixed_length))
    threads.append(thread)
    thread.start()

# 等待所有线程完成
for thread in threads:
    thread.join()

print("所有 eeprom 文件读取完成。")
