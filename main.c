#include "Server.h"
#include <stdio.h>
#include <stdlib.h>  // atoi()
#include <unistd.h>  // chdir()

int main(int argc, char* argv[])
{
    printf("跑起来！快乐不？\n");
    if (argc < 3)
    {
        printf("./a.out port path\n");
        return -1;
    }
    unsigned short port = atoi(argv[1]);

    //切换当前进程的工作目录
    chdir(argv[2]);

    // 初始化用于监听的套接字
    int lfd = initListenFd(port);

    // 启动服务器程序
    epollRun(lfd);

    //======================================================================
    //const char* s = "http://www.baidu.com:12345";
    //char protocol[32] = { 0 };
    //char host[128] = { 0 };
    //char port[8] = { 0 };
    //sscanf(s, "%[^:]://%[^:]:%[1-9]", protocol, host, port);

    //printf("protocol: %s", protocol); //结果为：http
    //printf("host: %s", host);         //结果为：www.baidu.com
    //printf("port: %s", port);         //结果为：12345

    return 0;
}