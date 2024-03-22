#ifndef __SERVER_H__
#define __SERVER_H__

// 初始化用于监听的套接字
int initListenFd(unsigned short port);

// 启动epoll
int epollRun(int lfd);

// 和客户端建立连接
//int acceptClient(int lfd, int epfd);
void* acceptClient(void* arg);

// 接受http请求                  
//int recvHttpRequset(int cfd, int epfd);
void* recvHttpRequset(void* arg);

// 解析请求行
int parseRequestLine(const char* line, int cfd);

// 发送文件
int sendFile(const char* fileName, int cfd);

// 获取响应文件类型
const char* getFileType(const char* name);

// 除数据块外，发送http响应（状态行+响应头+空行）
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length);

// 发送目录
int sendDir(const char* dirName, int cfd);

int hexToDec(char c);
void decodeMsg(char* to, char* from);

#endif // SERVER_H