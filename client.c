#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 256

const char* IP;
int PORT;
const char* ACTION;
int TIMES;
int AMOUNTS;
char sendBuf[BUFFER_SIZE];
int sock;

void socketInit(const char* ip,int port){
	struct sockaddr_in serverAddr;
	if((sock=socket(AF_INET,SOCK_STREAM,0))<0){
		perror("socket()");
		exit(EXIT_FAILURE);
	}
	memset(&serverAddr,0,sizeof(serverAddr));
	serverAddr.sin_family=AF_INET;
	serverAddr.sin_port=htons(port);
	if(inet_pton(AF_INET,ip,&serverAddr.sin_addr)<=0){
		perror("inet_pton()");
		close(sock);
		exit(EXIT_FAILURE);
	}
	if(connect(sock,(struct sockaddr*)&serverAddr,sizeof(serverAddr))<0){
		perror("connect()");
		close(sock);
		exit(EXIT_FAILURE);
	}
	printf("Connected to %s:%d\n",ip,port);
}

int main(int argc,char *argv[]){
	int retVal;
	if(argc!=6){
		fprintf(stderr,"Execution Parameters Error. Ex. %s <ip address> <port> <deposit/withdraw> <amount> <times>\n",argv[0]);
		return -1;
	}
	IP=argv[1];
	PORT=atoi(argv[2]);
	ACTION=argv[3];
	AMOUNTS=atoi(argv[4]);
	TIMES=atoi(argv[5]);
	snprintf(sendBuf,BUFFER_SIZE,"%s %d %d\n",ACTION,AMOUNTS,TIMES);
	socketInit(IP,PORT);
	retVal=write(sock,sendBuf,BUFFER_SIZE);
	if(retVal<0){
		perror("write to SOCK failed.\n");
		return -1;
	}
	shutdown(sock,SHUT_WR);
	return 0;
}
