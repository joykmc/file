#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    //if (argc != 2) {
    //    fprintf(stderr, "用法: %s <sysfs路径>\n", argv[0]);
    //    return 1;
    //}

    const char *path = argv[1]; // 从命令行参数中获取路径
    int fd;
    char buffer[256]; // 缓冲区，用于存储读取的数据
    ssize_t bytesRead;
    int i;
    char base[] = "/sys_switch/transceiver/eth";
    char suffix = "/present";
    char result[1000];
while(1) {
    
    for (i=0;i<128;i++) {
    //const char *tmp_path = "/sys_switch/transceiver/eth1/present"
    snprintf(result, sizeof(result), "%s%d%s", base, i, suffix);
    printf("result=%s\n", result);        

    // 打开 sysfs 文件
    fd = open(result, O_RDONLY);
    if (fd == -1) {
        perror("无法打开 sysfs 文件");
        return 1;
    }

    // 读取文件内容
    bytesRead = read(fd, buffer, sizeof(buffer) - 1);
    if (bytesRead == -1) {
        perror("读取 sysfs 文件失败");
        close(fd);
        return 1;
    }

    // 添加字符串结束符
    buffer[bytesRead] = '\0';

    // 输出原始数据
    printf("原始数据：%s\n", buffer);

    // 关闭文件
    close(fd);
}
}
    return 0;
}

