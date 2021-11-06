#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/timeb.h>

#define BUF_LEN 128

void *com_server(void *port_num) {
    char buffer[BUF_LEN] = { 0 }, filename[20] = { 0 };
    int client_fd, msg_len, msec;
    FILE *fp;
    struct sockaddr_in server_addr;
    time_t now;
    struct tm *lt;
    struct timeb timebuf;

    // 1. TCP/IP를 사용하는 소켓 생성
    // client_fd에 file descripter return
    if((client_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        printf("Server : Can't open stream socket\n");
        return 0;
    }

    // 2-1. sockaddr_in 구조체를 통해 서버의 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(*(int *)port_num);
    server_addr.sin_addr.s_addr = inet_addr("192.168.56.101");

    // 2. 서버에 connection을 요청
    if(connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("Port %d connection failed\n", *(int *)port_num);
        return 0;
    }
    else
    {
        printf("Connected to %d\n", *(int *)port_num);

        // connect 성공하면 텍스트 파일 생성
        sprintf(filename, "%d.txt", *(int *)port_num);
        fp = fopen(filename, "a");

        // 패킷을 수신할 때마다 텍스트 파일에 내용 저장
        while (1) {
            memset(buffer, 0, sizeof(buffer)); //buffer 초기화
            
            if ((msg_len = read(client_fd, buffer, BUF_LEN)) > 0) {
                time(&now);
                lt = localtime(&now); // h, m, s 가져오기
                ftime(&timebuf);
                msec = timebuf.millitm; // msec 가져오기

                fprintf(fp, "%02d:%02d:%02d.%03d %3d %s\n", lt->tm_hour, lt->tm_min, lt->tm_sec, msec, msg_len, buffer);
            }
        }
    }

    fclose(fp);
    close(client_fd);

    return 0;
}

int main() {
    pthread_t p_thread[5];
    int port_nums[5] = {4444, 5555, 6666, 7777, 8888};

    for (int i = 0; i < 5; i++) {
        // thread 생성
        if (pthread_create(&p_thread[i], NULL, com_server, (void *)&port_nums[i]) < 0) {
            printf("Thread create error\n");
            exit(0);
        }
    }

    for (int i = 0; i < 5; i++) {
        // tread join, thread 실행이 끝날 때까지 대기
        pthread_join(p_thread[i], NULL);
    }

    printf("Client finish\n");

    return 0;
}
