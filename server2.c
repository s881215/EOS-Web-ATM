#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>

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
/* 	shared memory	  */
/**************************/
#define SHM_SIZE sizeof(int)
#define SHM_KEY  0x1234
int shm_id;
int *balance_shm;


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

/********************/
/*   shm function   */
/********************/
static void shmCreate(){
	if((shm_id=shmget(SHM_KEY,SHM_SIZE,IPC_CREAT|0666))<0){
		perror("shmCreate(): shmget() failed.\n");
		exit(EXIT_FAILURE);
	}
	if((balance_shm=shmat(shm_id,NULL,0))==(int*)-1){
		perror("shmCreate(): shmat() failed.\n");
		exit(EXIT_FAILURE);
	}
}

/********************/
/* process function */
/********************/
static void childProcess(int* fd){
	int CFD=*fd;
	int n;
	char action[10];
	int times;
	int amount;
	char recBuf[BUFFER_SIZE];
	int *balance=balance_shm;
	n=read(CFD,recBuf,BUFFER_SIZE-1);
	if(n<0){
		perror("childProcess(): read() failed.\n");
		exit(EXIT_FAILURE);
	}
	recBuf[n]='\0';
	if(sscanf(recBuf,"%9s %d %d",action,&amount,&times)!=3){
		perror("childProcess(): sscanf() failed.\n");
		exit(EXIT_FAILURE);
	}
	for(n=0;n<times;n++){
		P(sem);
		if(strcmp(action,"deposit")==0){
			(*balance) += amount;
			printf("After deposit: %d\n",*balance);
		}else if(strcmp(action,"withdraw")==0){
			(*balance) -= amount;
			printf("After withdraw: %d\n",*balance);
		}else{
			perror("Unknown Command.\n");
			V(sem);
			break;
		}
		V(sem);
		usleep(500);
	}
}

/*********************************/
/* clean socket fd and semaphore */
/*********************************/
static void cleanSocketSemaphore(int sig){
	close(listenFd);
	if(semctl(sem,0,IPC_RMID,0)<0){
		perror("cleanSocketSemaphore(): semctl() failed\n");
		exit(EXIT_FAILURE);
	}else{
		printf("\nRemove semaphore id successfully\n");
	}
	shmdt(balance_shm);
	if(shmctl(shm_id,IPC_RMID,NULL)<0){
		perror("cleanSocketSemaphore(): shmctl() failed\n");
		exit(EXIT_FAILURE);
	}else{
		printf("Remoce shared memory id successfully\n");
	}
	exit(EXIT_SUCCESS);
}

/************************/
/* close zombie process */
/************************/
static void closeZombie(int sig){
	while(waitpid(-1,NULL,WNOHANG)>0);
}

int main(int argc,char* argv[]){
	pid_t pid;
	if(argc!=2){
		fprintf(stderr,"Execution failed. ex: %s <port>\n",argv[0]);
		return -1;
	}
	semaphoreInit();
	signal(SIGINT,cleanSocketSemaphore);
	signal(SIGCHLD,closeZombie);
	socket_Listen(atoi(argv[1]));
	shmCreate();
	while(1){
		accept_Connection();	
		printf("Connection Successfully\n");
		pid=fork();
		if(pid<0){
			perror("main(): fork() failed.\n");
			continue;
		}
		if(pid==0){
			close(listenFd);
			childProcess(clientFd);
			exit(EXIT_SUCCESS);
		}
		close(*clientFd);
		free(clientFd);
	}
	return EXIT_SUCCESS;
}
