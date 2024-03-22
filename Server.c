#include "Server.h"
#include <stdio.h>			// sscanf(),perror(),sprintf()
#include <arpa/inet.h>		// socket系列
#include <sys/epoll.h>		// epoll系列
#include <fcntl.h>			// 边沿非阻塞 fcntl()
#include <errno.h>			// errno,EAGAIN
#include <strings.h>		// strcasecmp():不区分大小写
#include <string.h>			// memcpy(),memset(),strcmp(),strrchr(),strstr()
#include <sys/stat.h>       // 文件属性系列
#include <assert.h>         // 断言
#include <sys/sendfile.h>   // sendfile()
#include <sys/types.h>      // lseek()
#include <unistd.h>         // lseek(),chdir()
#include <dirent.h>
#include <stdlib.h>         // atoi(), malloc() free()
#include <ctype.h>
#include <pthread.h>

struct FdInfo
{
	int fd;   //监听的或者通信的文件描述符
	int epfd; //epoll树的根节点
	pthread_t tid;
};

int initListenFd(unsigned short port)
{
	// 1.创建监听的fd
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1)
	{
		perror("socket");
		return -1;
	}

	// 2.设置端口复用
	int opt = 1;
	int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
	if (ret == -1)
	{
		perror("setsockopt");
		return -1;
	}

	// 3.绑定
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

	// 4.设置监听
	ret = listen(lfd, 128);
	if (ret == -1)
	{
		perror("listen");
		return -1;
	}

	// 5.返回监听的fd
	return lfd;
}

int epollRun(int lfd)
{
	// 1.创建epoll实例
	int epfd = epoll_create(1);
	if (epfd == -1)
	{
		perror("epoll_create");
		return -1;
	}

	// 2.lfd 上树
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	if (ret == -1)
	{
		perror("epoll_ctl");
		return -1;
	}

	// 3.检测
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
				// 建立新连接 accept
				//acceptClient(lfd, epfd);
				pthread_create(&info->tid, NULL, acceptClient, info);
			}
			else
			{
				// 主要是接受对端的数据
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
	// 1.建立连接
	int cfd = accept(info->fd, NULL, NULL);
	if (cfd == -1)
	{
		perror("accept");
		return NULL;
	}

	// 2.设置边沿非阻塞
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK; //非阻塞
	fcntl(cfd, F_SETFL, flag);

	// 3.cfd添加到epoll中
	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET; //边沿模式
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

/*-------------------------------------- 接受http请求 ------------------------------------------------*/
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
	// 判断数据是否被接受完毕
	if (len == -1 && errno == EAGAIN)   //数据接受完毕
	{
		// 解析请求行
		char* pt = strstr(buf, "\r\n"); //在大的字符串中寻找子串
		int reqLen = pt - buf;
		buf[reqLen] = '\0';
		parseRequestLine(buf, info->fd);
	}
	else if (len == 0)
	{
		// 客户端断开了连接
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
	// 解析请求行
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
	// 处理客户端请求的静态资源（目录或文件）
	char* file = NULL;
	if (strcmp(path, "/") == 0)
	{
		file = "./";     //请求的是资源目录
	}
	else
	{
		file = path + 1; //请求的是资源文件
	}

	/*-------------------------------------- 响应http请求 ------------------------------------------------*/
	// 获取文件属性
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		// 文件不存在 --回复404
		sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
		sendFile("404.html", cfd);
		return 0;
	}
	// 判断文件类型
	if (S_ISDIR(st.st_mode))
	{
		// 把这个目录中的内容发送给客户端
		printf("The request is for a directory...\n");
		sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
		sendDir(file, cfd);
	}
	else
	{
		// 把文件的内容发送给客户端
		printf("The request is for a file...\n");
		sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
		sendFile(file, cfd);
	}

	return 0;
}

const char* getFileType(const char* name)
{
	// a.jpg, a.mp4, a.html........
	// 自右向左查找'.'字符， 如果不存在返回null
	const char* dot = strrchr(name, '.');
	if (dot == NULL)
		return "text/plain; charset=utf-8"; //纯文本
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
	// 1.打开文件
	int fd = open(fileName, O_RDONLY); //只读方式打开
	if (fd <= 0)
	{
		printf("file fd: %d\n", fd);
		perror("open:");
	}
	assert(fd > 0);
	printf("Start sending a File...\n");
#if 1
	// 每次从文件中读1k,发送1k数据。
	while (1)
	{
		char buf[1024] = { 0 };
		int len = read(fd, buf, sizeof(buf));
		if (len > 0)
		{
			send(cfd, buf, len, MSG_NOSIGNAL);
			usleep(10); //发送数据慢一些，留给浏览器处理数据的时间
		}
		else if (len == 0)
		{
			break;      //文件发送完了
		}
		else
		{
			perror("read"); //异常
		}
	}
	// 直接发送文件
#else
	off_t offset = 0;
	int size = lseek(fd, 0, SEEK_END); //求文件大小
	printf("size: %ld\n", size);
	lseek(fd, 0, SEEK_SET);            //把文件指针移动到文件头
	printf("============================01\n");
	while (offset < size)
	{
		printf("============================02\n");
		int ret = sendfile(cfd, fd, &offset, size - offset);
		printf("ret value: %d\n", ret);
		if (ret == -1 && errno == EAGAIN)
		{
			printf("没有数据 no data......\n");
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
	// 状态行
	char buf[4096] = { 0 };
	sprintf(buf, "http/1.1 %d %s\r\n", status, descr);
	// 响应头和空行
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
		// 从namelist中取出文件名    namelist指向的是一个指针数组 比如:struct dirent* tmp[]
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


// 将16进制的字符转换为10进制的整形
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

// 解码
// to 存储解码之后的数据，传出参数， from被解码的数据，传入参数
void decodeMsg(char* to, char* from)
{
	for (; *from != '\0'; ++to, ++from)
	{
		// isxdigit -> 判断字符是不是16进制格式，取值在o~f
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
		{
			// 将16进制的数 -> 十进制， 将这个数值赋值给字符 int -> char(隐式转换)
			*to = hexToDec(from[1]) * 16 + hexToDec(from[2]);  //将3个字符变成一个字符，这个字符就是原始数据

			// 跳过 from[1] 和 from[2] 因为在当前循环中已经处理过了
			from += 2;
		}
		else
		{
			// 字符拷贝，赋值
			*to = *from;
		}
	}
	*to = '\0';
}