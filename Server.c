#include "Server.h"
#include <stdio.h>			// sscanf(),perror(),sprintf()
#include <arpa/inet.h>		// socketϵ��
#include <sys/epoll.h>		// epollϵ��
#include <fcntl.h>			// ���ط����� fcntl()
#include <errno.h>			// errno,EAGAIN
#include <strings.h>		// strcasecmp():�����ִ�Сд
#include <string.h>			// memcpy(),memset(),strcmp(),strrchr(),strstr()
#include <sys/stat.h>       // �ļ�����ϵ��
#include <assert.h>         // ����
#include <sys/sendfile.h>   // sendfile()
#include <sys/types.h>      // lseek()
#include <unistd.h>         // lseek(),chdir()
#include <dirent.h>
#include <stdlib.h>         // atoi(), malloc() free()
#include <ctype.h>
#include <pthread.h>

struct FdInfo
{
	int fd;   //�����Ļ���ͨ�ŵ��ļ�������
	int epfd; //epoll���ĸ��ڵ�
	pthread_t tid;
};

int initListenFd(unsigned short port)
{
	// 1.����������fd
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1)
	{
		perror("socket");
		return -1;
	}

	// 2.���ö˿ڸ���
	int opt = 1;
	int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
	if (ret == -1)
	{
		perror("setsockopt");
		return -1;
	}

	// 3.��
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1)
	{
		perror("bind");
		return -1;
	}

	// 4.���ü���
	ret = listen(lfd, 128);
	if (ret == -1)
	{
		perror("listen");
		return -1;
	}

	// 5.���ؼ�����fd
	return lfd;
}

int epollRun(int lfd)
{
	// 1.����epollʵ��
	int epfd = epoll_create(1);
	if (epfd == -1)
	{
		perror("epoll_create");
		return -1;
	}

	// 2.lfd ����
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	if (ret == -1)
	{
		perror("epoll_ctl");
		return -1;
	}

	// 3.���
	struct epoll_event evs[1024];
	int size = sizeof(evs) / sizeof(struct epoll_event);
	while (1)
	{
		int num = epoll_wait(epfd, evs, size, -1);
		for (int i = 0; i < num; ++i)
		{
			struct FdInfo* info = (struct FdInfo*)malloc(sizeof(struct FdInfo));
			int fd = evs[i].data.fd;
			info->epfd = epfd;
			info->fd = fd;
			if (fd == lfd)
			{
				// ���������� accept
				//acceptClient(lfd, epfd);
				pthread_create(&info->tid, NULL, acceptClient, info);
			}
			else
			{
				// ��Ҫ�ǽ��ܶԶ˵�����
				//recvHttpRequset(fd, epfd);
				pthread_create(&info->tid, NULL, recvHttpRequset, info);
			}
		}
	}

	return 0;
}

//int acceptClient(int lfd, int epfd)
void* acceptClient(void* arg)
{
	struct FdInfo* info = (struct FdInfo*)arg;
	// 1.��������
	int cfd = accept(info->fd, NULL, NULL);
	if (cfd == -1)
	{
		perror("accept");
		return NULL;
	}

	// 2.���ñ��ط�����
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK; //������
	fcntl(cfd, F_SETFL, flag);

	// 3.cfd��ӵ�epoll��
	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET; //����ģʽ
	int ret = epoll_ctl(info->epfd, EPOLL_CTL_ADD, cfd, &ev);
	if (ret == -1)
	{
		perror("epoll_ctl");
		return NULL;
	}

	printf("acceptclit threadId: %ld\n", info->tid);
	free(info);
	return NULL;
}

/*-------------------------------------- ����http���� ------------------------------------------------*/
//int recvHttpRequset(int cfd, int epfd) 
void* recvHttpRequset(void* arg)
{
	struct FdInfo* info = (struct FdInfo*)arg;
	printf("\nstart communication.....\n");
	int len = 0, total = 0;
	char tmp[1024];
	char buf[4096];
	while ((len = recv(info->fd, tmp, sizeof(tmp), 0)) > 0)
	{
		if (total + len < sizeof(buf))
		{
			memcpy(buf + total, tmp, len);
			memset(tmp, 0, sizeof(tmp));
		}
		total += len;
	}
	// �ж������Ƿ񱻽������
	if (len == -1 && errno == EAGAIN)   //���ݽ������
	{
		// ����������
		char* pt = strstr(buf, "\r\n"); //�ڴ���ַ�����Ѱ���Ӵ�
		int reqLen = pt - buf;
		buf[reqLen] = '\0';
		parseRequestLine(buf, info->fd);
	}
	else if (len == 0)
	{
		// �ͻ��˶Ͽ�������
		epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
		close(info->fd);
	}
	else
	{
		perror("recv");
	}

	printf("recvMsg threadId: %ld\n", info->tid);
	free(info);
	return NULL;
}

int parseRequestLine(const char* line, int cfd)
{
	// ����������
	char method[12] = { 0 };
	char path[1024] = { 0 };
	char version[16] = { 0 };
	sscanf(line, "%[^ ] %[^ ] %[^\r\n]", method, path, version);
	decodeMsg(path, path);
	printf("method: %s, path: %s, version: %s\n", method, path, version);
	if (strcasecmp(method, "get") != 0)
	{
		return -1;
	}
	// ����ͻ�������ľ�̬��Դ��Ŀ¼���ļ���
	char* file = NULL;
	if (strcmp(path, "/") == 0)
	{
		file = "./";     //���������ԴĿ¼
	}
	else
	{
		file = path + 1; //���������Դ�ļ�
	}

	/*-------------------------------------- ��Ӧhttp���� ------------------------------------------------*/
	// ��ȡ�ļ�����
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		// �ļ������� --�ظ�404
		sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
		sendFile("404.html", cfd);
		return 0;
	}
	// �ж��ļ�����
	if (S_ISDIR(st.st_mode))
	{
		// �����Ŀ¼�е����ݷ��͸��ͻ���
		printf("The request is for a directory...\n");
		sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
		sendDir(file, cfd);
	}
	else
	{
		// ���ļ������ݷ��͸��ͻ���
		printf("The request is for a file...\n");
		sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
		sendFile(file, cfd);
	}

	return 0;
}

const char* getFileType(const char* name)
{
	// a.jpg, a.mp4, a.html........
	// �����������'.'�ַ��� ��������ڷ���null
	const char* dot = strrchr(name, '.');
	if (dot == NULL)
		return "text/plain; charset=utf-8"; //���ı�
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/X-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/X-ns-proxy-autoconfig";

	return "text/plain; charset=utf-8";
}

int sendFile(const char* fileName, int cfd)
{
	// 1.���ļ�
	int fd = open(fileName, O_RDONLY); //ֻ����ʽ��
	if (fd <= 0)
	{
		printf("file fd: %d\n", fd);
		perror("open:");
	}
	assert(fd > 0);
	printf("Start sending a File...\n");
#if 1
	// ÿ�δ��ļ��ж�1k,����1k���ݡ�
	while (1)
	{
		char buf[1024] = { 0 };
		int len = read(fd, buf, sizeof(buf));
		if (len > 0)
		{
			send(cfd, buf, len, MSG_NOSIGNAL);
			usleep(10); //����������һЩ������������������ݵ�ʱ��
		}
		else if (len == 0)
		{
			break;      //�ļ���������
		}
		else
		{
			perror("read"); //�쳣
		}
	}
	// ֱ�ӷ����ļ�
#else
	off_t offset = 0;
	int size = lseek(fd, 0, SEEK_END); //���ļ���С
	printf("size: %ld\n", size);
	lseek(fd, 0, SEEK_SET);            //���ļ�ָ���ƶ����ļ�ͷ
	printf("============================01\n");
	while (offset < size)
	{
		printf("============================02\n");
		int ret = sendfile(cfd, fd, &offset, size - offset);
		printf("ret value: %d\n", ret);
		if (ret == -1 && errno == EAGAIN)
		{
			printf("û������ no data......\n");
			/*perror("sendfile");*/
		}
	}

#endif
	printf("END sending a File...\n");
	return 0;
}

int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length)
{
	printf("START Response status line, header and blank line... \n");
	// ״̬��
	char buf[4096] = { 0 };
	sprintf(buf, "http/1.1 %d %s\r\n", status, descr);
	// ��Ӧͷ�Ϳ���
	sprintf(buf + strlen(buf), "content-type: %s\r\n", type);
	sprintf(buf + strlen(buf), "content-length: %d\r\n\r\n", length);
	//sprintf(buf + strlen(buf), "\r\n");

	send(cfd, buf, strlen(buf), 0);
	printf("END Response status line, header and blank line... \n");
	return 0;
}

int sendDir(const char* dirName, int cfd)
{
	printf("Start sending a directory...\n");
	char buf[4096] = { 0 };
	sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);

	struct dirent** namelist;
	int num = scandir(dirName, &namelist, NULL, alphasort);
	for (int i = 0; i < num; ++i)
	{
		// ��namelist��ȡ���ļ���    namelistָ�����һ��ָ������ ����:struct dirent* tmp[]
		char* name = namelist[i]->d_name;
		struct stat st;
		char subPath[1024] = { 0 };
		sprintf(subPath, "%s/%s", dirName, name);
		stat(subPath, &st);
		if (S_ISDIR(st.st_mode))
		{
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		else
		{
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		send(cfd, buf, strlen(buf), 0);
		memset(buf, 0, sizeof(buf));
		free(namelist[i]);
	}
	sprintf(buf, "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
	printf("Send Directory End...\n");
	free(namelist);
	return 0;
}


// ��16���Ƶ��ַ�ת��Ϊ10���Ƶ�����
int hexToDec(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}

// ����
// to �洢����֮������ݣ����������� from����������ݣ��������
void decodeMsg(char* to, char* from)
{
	for (; *from != '\0'; ++to, ++from)
	{
		// isxdigit -> �ж��ַ��ǲ���16���Ƹ�ʽ��ȡֵ��o~f
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
		{
			// ��16���Ƶ��� -> ʮ���ƣ� �������ֵ��ֵ���ַ� int -> char(��ʽת��)
			*to = hexToDec(from[1]) * 16 + hexToDec(from[2]);  //��3���ַ����һ���ַ�������ַ�����ԭʼ����

			// ���� from[1] �� from[2] ��Ϊ�ڵ�ǰѭ�����Ѿ��������
			from += 2;
		}
		else
		{
			// �ַ���������ֵ
			*to = *from;
		}
	}
	*to = '\0';
}