#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#define BUFFER_SIZE 256

/*****************************/
/* socket related parameters */
/*****************************/

#define MULTI_CONNECTION 4

static int listenFd;
struct sockaddr_in serverAddr;

struct sockaddr_in client;
socklen_t clientLen=sizeof(client);
int* clientFd;

/********************/
/* binary semaphore */
/********************/
#define semKey 311512046
union semun{
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};
static int sem;

/**************************/
/* ATM related parameters */
/**************************/
int balance=0;		   // initial money in ATM

/*******************/
/* socket function */
/*******************/
void socket_Listen(int port){
	listenFd=socket(AF_INET,SOCK_STREAM,0);
	if(listenFd<0){
		perror("socket() failed");
		exit(EXIT_FAILURE);
	}
	serverAddr.sin_family=AF_INET;
	serverAddr.sin_addr.s_addr=INADDR_ANY;
	serverAddr.sin_port=htons(port);
	if(bind(listenFd,(struct sockaddr*)&serverAddr,sizeof(serverAddr))<0){
		perror("bind() failed");
		exit(EXIT_FAILURE);
	}
	if(listen(listenFd,MULTI_CONNECTION)<0){
		perror("listen() failed");
		exit(EXIT_FAILURE);
	}
}

void accept_Connection(){
	clientFd=malloc(sizeof(int));
	if(clientFd==NULL){
		printf("accept_Connection(): malloc() failed\n");
		
	}
	*clientFd=accept(listenFd,(struct sockaddr*)&client,&clientLen);
	if(*clientFd<0){
		printf("accept_Connection(): accept() failed\n");
		
	}
}

/********************************************/
/* P() lock & V() release & semaphoreInit() */
/********************************************/
static int P(int s){
	struct sembuf sop;
	sop.sem_num=0;
	sop.sem_op=-1;
	sop.sem_flg=0;
	if(semop(s,&sop,1)<0){
		perror("P(): semop() failed");
		return -1;
	}
	return 0;
}
static int V(int s){
	struct sembuf sop;
	sop.sem_num=0;
	sop.sem_op=1;
	sop.sem_flg=0;
	if(semop(s,&sop,1)<0){
		perror("V(): semop() failed");
		return -1;
	}
	return 0;
}
static void semaphoreInit(){
	sem=semget(semKey,1,IPC_CREAT|IPC_EXCL|0666);
	if(sem<0){
		perror("semget() failed");
		exit(EXIT_FAILURE);
	}
	union semun aaa;
	aaa.val=1;
	if(semctl(sem,0,SETVAL,aaa)<0){
		perror("semctl() failed");
		exit(EXIT_FAILURE);
	}
}

/*******************/
/* thread function */
/*******************/
static void *workerThread(void* arg){
	int idx;
	int n;
	// get the client file descriptor
	int CFD=*(int*)arg;
	free(arg);
	char recBuf[BUFFER_SIZE];
	while(1){
		if((n=read(CFD,recBuf,BUFFER_SIZE-1))<0){
			perror("workerThread(): read() failed.");
			break;
		}
		recBuf[n]='\0';
		char action[10];
		int amount;
		int times;
		if(sscanf(recBuf,"%9s %d %d",action,&amount,&times)!=3){
			//printf("workerThread(): sscanf() failed.\n");
			break;
		}
		printf("client %d: %s,%d,%d\n",CFD,action,amount,times);
		for(idx=0;idx<times;idx++){
			P(sem);
			if(strcmp(action,"deposit")==0){
				balance+=amount;	
				printf("After deposite: %d\n",balance);	
			}else if(strcmp(action,"withdraw")==0){
				balance-=amount;
				printf("After withdraw: %d\n",balance);
			}else{
				printf("Unknown Command\n");
				V(sem);
				break;
			}
			V(sem);
			usleep(200);
		}	
	}
	close(CFD);
	return NULL;
}

/*********************************/
/* clean socket fd and semaphore */
/*********************************/
static void cleanSocketSemaphore(int sig){
	close(listenFd);
	if(semctl(sem,0,IPC_RMID,0)<0){
		perror("cleanSocketSemaphore(): semctl() failed");
		exit(EXIT_FAILURE);
	}else{
		printf("Remove semaphore id successfully\n");
	}
	exit(EXIT_SUCCESS);
}

int main(int argc,char* argv[]){
	if(argc!=2){
		fprintf(stderr,"Execution failed. ex: %s <port>\n",argv[0]);
		return -1;
	}
	semaphoreInit();
	signal(SIGINT,cleanSocketSemaphore);
	socket_Listen(atoi(argv[1]));
	while(1){
		accept_Connection();	
		printf("Connection Successfully\n");
		pthread_t tid;
		pthread_create(&tid,NULL,workerThread,clientFd);
		pthread_detach(tid);
	}
	return EXIT_SUCCESS;
}
