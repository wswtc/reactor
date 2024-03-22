#ifndef __SERVER_H__
#define __SERVER_H__

// ��ʼ�����ڼ������׽���
int initListenFd(unsigned short port);

// ����epoll
int epollRun(int lfd);

// �Ϳͻ��˽�������
//int acceptClient(int lfd, int epfd);
void* acceptClient(void* arg);

// ����http����                  
//int recvHttpRequset(int cfd, int epfd);
void* recvHttpRequset(void* arg);

// ����������
int parseRequestLine(const char* line, int cfd);

// �����ļ�
int sendFile(const char* fileName, int cfd);

// ��ȡ��Ӧ�ļ�����
const char* getFileType(const char* name);

// �����ݿ��⣬����http��Ӧ��״̬��+��Ӧͷ+���У�
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length);

// ����Ŀ¼
int sendDir(const char* dirName, int cfd);

int hexToDec(char c);
void decodeMsg(char* to, char* from);

#endif // SERVER_H