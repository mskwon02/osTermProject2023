#include <sys/msg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <queue>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>
#include <unistd.h>
#include <iostream>

//Pcb state들 지정
#define RUNNING 0
#define READY 1
#define WAITING 2
#define TERMINATED 3


using namespace std;

// ipc메시지 구조체
struct Msg {
  //누구한테 보내는 메시지인지 구분할 수 있는 변수
  long type;
  //내용물
  int content;
};

//프로세스 구조체
struct Pcb {
  long pid;
  int remainCpuBurst;
  int ioStartTick=0;
  int ioDurationTIck=0;
};

//ipc 메시지큐의 고유 키값
int msgQueueKey =1398;

//timeslice 설정
int timeslice = 5;

Pcb* runningPcb;

//준비큐 생성
queue<Pcb *> readyQueue;
// IO큐 생성
queue<Pcb *> ioQueue;



//타임틱 지난 횟수 세는 변수
int timeTickPassed;

//현재 running중인 pcb가 몇 틱동안 cpu 사용했는지 저장 -> timeslice보다 커지면 context swtich 일어나야함
int runningPcbRunTick=0;

void dispatch();

//현재 상황 log로 찍는 함수
void printLog(){

  cout<<"----------타임 틱 "<<timeTickPassed<<"번째 실행 후 ----------"<<endl;
  if(runningPcb==NULL){
    cout<<"running pid: "<<"None"<<endl;
    cout<<"remain cpu burst: "<<"None"<<endl<<endl;
  }
  else{
    cout<<"running pid: "<<runningPcb->pid<<endl;
    cout<<"remain cpu burst: "<<runningPcb->remainCpuBurst<<endl<<endl;

  }

  //여기 함수로 만들어 빼기
  queue<Pcb*> tempReadyQueue = readyQueue;
  queue<Pcb*> tempIoQueue = ioQueue;

  cout << "ready Queue[pid, remain cpu burst]\n: ";
  while (!tempReadyQueue.empty()) {
    Pcb* current = tempReadyQueue.front();
    cout << "[" << current->pid << "," << current->remainCpuBurst << "]";
    cout << " | ";
    tempReadyQueue.pop();
  }
  cout<<endl<<endl;

  cout << "IO Queue[pid, remain IO burst]\n: ";
  while (!tempIoQueue.empty()) {
    Pcb* current = tempIoQueue.front();
    cout << "[" << current->pid << "," << current->remainCpuBurst << "]";
    cout << " | ";
    tempIoQueue.pop();
  }
  cout<<endl<<endl;

}

//타임 틱마다 실행될 내용
void perTimeTick(int signum){

  //현재 runnin중인 pcb의 remain cpu burst가 0이면
  if(runningPcb->remainCpuBurst==0){

    Msg msg;
    msg.type=runningPcb->pid;
    msg.content=TERMINATED;

    //종료하라는 메시지 보낸다
    msgsnd(msgQueueKey,&msg,10,IPC_NOWAIT);

    //ready큐에서 다음 실행할 프로세스 선택한다
    dispatch();
  }

  runningPcbRunTick++;

  if(runningPcbRunTick>timeslice){
    dispatch();
  }else{
    (runningPcb->remainCpuBurst)--;
  }

  // for(auto &i : ioQueue){

  // }

  
  //디스패치 조건 확인

  //디스패치 되는 경우
  //1. 현재 프로세스의 cpu burst 0인 경우
  //2. 현재 프로세스 io 시작시간인 경우

  timeTickPassed++;
  //log 찍기
  printLog();
}

//디스패치 함수
void dispatch(){
  
  //현재 실행중이 pcb의 remainCpuBurst가 남아있다면 ready큐의 맨 뒤에 넣어야함
  if(runningPcb->remainCpuBurst >0){
    readyQueue.push(runningPcb);
  }

  //ready큐에 뭔가가 있다면
  if(!readyQueue.empty()){

    //맨 앞의 pcb를 꺼내 현재 실행중인 pcb로 설정
    runningPcb=readyQueue.front();
    readyQueue.pop();
    runningPcbRunTick=0;

    Msg msg;
    msg.type=runningPcb->pid;
    msg.content=RUNNING;

    //꺼낸 pcb의 pid와 동일한 자식프로세스에게 너 실행해 라는 메시지 전송
    msgsnd(msgQueueKey,&msg,10,IPC_NOWAIT);
  }
}
//ready큐 맨 앞 하나 가져오기
//io할지 안할지 결정
  //io 한다면 pcb의 io시작시간, io지속시간 기록 



int main() {

  srand(time(NULL));
  
  pid_t pid, child_pid, parent_pid;
  parent_pid=getpid();

  int msgQueId;

  //키가 msgQueueKey인 메시지큐 생성. msgQueId가 메시지큐를 식별할 수 있는 ID역할
  msgQueId = msgget(msgQueueKey, IPC_CREAT | 0666);
  if(msgQueId==-1){
    cout<<"msgget error"<<endl;
  }


  //타임 틱 설정
  struct itimerval timer;
  memset(&timer, 0, sizeof(timer));

  // 0.3초 설정 (300000 마이크로초)
  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 300000; // 타이머 시작 시간
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 300000; // 타이머 반복 간격


  // SIGALRM 핸들러 등록
  // SIGALRM: 이 신호는 setitimer() 함수로 예약한 타이머가 만료되었을 때 발생
  // 내가 설정한 타이머 시간 단위마다 perTimeTick 함수가 동작한다
  signal(SIGALRM, perTimeTick);

  cout<<"=========== OS Term Project ==========="<<endl<<endl;
  cout<<"Time Slice: "<<timeslice<<endl<<endl;

  //자식 프로세스들 생성 파트
  for(int i=0; i<10;i++){

    //프로세스 생성
    long newPid=fork();

    //부모 프로세스에서 코드
    if (newPid > 0)
        {
          Pcb* pcbP= new Pcb;
            
          pcbP->pid=newPid;
          // cpuburst의 범위는 1~30
          pcbP->remainCpuBurst=rand()%29 +1;

          readyQueue.push(pcbP);
           
        }
        
        //자식 프로세스에서 코드
        else if (newPid == 0)
        {
          long msgIdx=getpid();

          int doIo, ioStartTick, ioDurationTick;
          

          Msg msg;
          msg.type = msgIdx;
          
          while(1){
            //메시지 큐에서 가져올 게 있는 경우
            if((msgrcv(msgQueId, &msg, 50, msgIdx,0)) != -1){

              //현재 상태가 running하라는 메시지였으면
              if (msg.content==RUNNING){
                //io작업 할까 말까
                doIo=rand()%2;
                if(doIo==1){
                  //timeslice내(1~timeslice-1)에서 시작시간 지정 
                  ioStartTick = rand()%(timeslice-2) +1;
                  //1~50내로 io지속시간 설정
                  ioDurationTick = rand()%49 +1;


                }

              }

              //현재 pcb상태가 terminated하라는 메시지였으면
              else if(msg.content==TERMINATED){
                exit(0);
              }
            
            }

          }
          

        }
        else if (pid < 0)
        {
            printf("fork 실패!\n");
            exit(1);
        }

  }

  //0번쨰 타임 틱에는 readyQueue의 첫 번째 pcb뽑아서 넣는다
  runningPcb=readyQueue.front();
  readyQueue.pop();

  // 타이머 설정
  // alarm함수는 초 단위로만 타이머를 설정할 수 있음. 
  // 분 수초 타이머 설정하려면 setitimer함수 사용해야함
  setitimer(ITIMER_REAL, &timer, NULL);

  while(1){
    
  };
  
  return 0; }