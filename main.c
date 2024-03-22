#include "Server.h"
#include <stdio.h>
#include <stdlib.h>  // atoi()
#include <unistd.h>  // chdir()

int main(int argc, char* argv[])
{
    printf("�����������ֲ���\n");
    if (argc < 3)
    {
        printf("./a.out port path\n");
        return -1;
    }
    unsigned short port = atoi(argv[1]);

    //�л���ǰ���̵Ĺ���Ŀ¼
    chdir(argv[2]);

    // ��ʼ�����ڼ������׽���
    int lfd = initListenFd(port);

    // ��������������
    epollRun(lfd);

    //======================================================================
    //const char* s = "http://www.baidu.com:12345";
    //char protocol[32] = { 0 };
    //char host[128] = { 0 };
    //char port[8] = { 0 };
    //sscanf(s, "%[^:]://%[^:]:%[1-9]", protocol, host, port);

    //printf("protocol: %s", protocol); //���Ϊ��http
    //printf("host: %s", host);         //���Ϊ��www.baidu.com
    //printf("port: %s", port);         //���Ϊ��12345

    return 0;
}