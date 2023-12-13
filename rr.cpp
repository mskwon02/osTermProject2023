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

#include <map>

//Pcb state들 지정
#define START 0
#define RUNNING 1
#define READY 2
#define WAITING 3
#define TERMINATED 4

//전체 자식 프로세스 갯수
#define CHILDPROCESSNUM 5

pid_t parentPid;

using namespace std;
// ipc메시지 구조체
struct Msg {
	//누구한테 보내는 메시지인지 구분할 수 있는 변수
	long type;
	//내용물
	int content;
}
;
//프로세스 구조체
struct Pcb {
	long pid;
	int remainCpuBurst;
	int ioStartTick=-1;
	int remainIoBurst=-1;
	bool doIo=0;
  bool firstRun=true;

}
;

struct ProcessTimeInfo{

  //프로세스가 처음으로 ready큐에 들어온 시간(틱)
  int arrivalTick=0;
  //프로세스가 처음으로 run된 시간(틱)
  int firstRunTick;
  //프로세스가 완료될때까지(cpu burst 0될때까지) 걸린 시간(틱)
  int CompleteTick;
};

//스케쥴링 성능(scheduling metric) 확인할 정보 담는 map객체
map<int, ProcessTimeInfo> schedulingMetricMap;

//timeslice 설정
int timeslice = 5;

//메시지큐 식별할 수 있는 ID저장 변수
int msgQueId;

Pcb* runningPcb;
//준비큐 생성
queue<Pcb *> readyQueue;
// IO큐 생성
queue<Pcb *> ioQueue;
//타임틱 지난 횟수 세는 변수
int timeTickPassed;
//현재 running중인 pcb가 몇 틱동안 cpu 사용했는지 저장 -> timeslice보다 커지면 context swtich 일어나야함
int runningPcbRunTick=0;
void contextSwitch();
void dispatch();
//현재 상황 log로 찍는 함수
void printLog() {
	cout<<"----------"<<timeTickPassed<<" 번째 타임틱 ----------"<<endl;
	if(runningPcb==NULL) {
		cout<<"현재 cpu 차지 pid: "<<"None"<<endl;
		cout<<"remain cpu burst: "<<"None"<<endl<<endl;
	} else {
		cout<<"현재 cpu 차지 pid: "<<runningPcb->pid<<endl;
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
	cout << "IO Queue[pid, remain CPU burst, remain IO burst]\n: ";
	while (!tempIoQueue.empty()) {
		Pcb* current = tempIoQueue.front();
		cout << "[" << current->pid << "," <<current->remainCpuBurst<<", "<< current->remainIoBurst << "]";
		cout << " | ";
		tempIoQueue.pop();
	}
	cout<<endl<<endl;
	cout<<"runningPcbRunTick: "<<runningPcbRunTick<<endl;
}

//--------타임 틱마다 실행될 내용
void perTimeTick(int signum) {

	//io큐에 있는 프로세스들의 remainIoBurst 감소
	queue<Pcb*> tempIoQueue;
	cout<<"start ioBurst minus"<<endl;
	while (!ioQueue.empty()) {
		Pcb* currentIoPcb = ioQueue.front();
		currentIoPcb->remainIoBurst--;
		//remainIoBurst0이면 ready큐로 들어간다
		if(currentIoPcb->remainIoBurst<=0){
			currentIoPcb->remainIoBurst = -1;
			currentIoPcb->ioStartTick = -1;
			readyQueue.push(currentIoPcb);
		}else{
			tempIoQueue.push(currentIoPcb);
		}	
		ioQueue.pop();
	}
	ioQueue = tempIoQueue;
	cout<<"end ioBurst minus"<<endl;

	// queue<Pcb*> tempIoQueue = ioQueue;
	// cout<<"start ioBurst minus"<<endl;
	// while (!tempIoQueue.empty()) {
	// 	Pcb* currentIoPcb = tempIoQueue.front();
	// 	currentIoPcb->remainIoBurst--;
	// 	//remainIoBurst0이면 ready큐로 들어간다
	// 	if(currentIoPcb->remainIoBurst==0){
	// 		readyQueue.push(currentIoPcb);
	// 	}else{
	// 		ioQueue.push(currentIoPcb);
	// 	}	
	// 	tempIoQueue.pop();
	// }
	// cout<<"end ioBurst minus"<<endl;


    // 현재 실행 중인 pcb가 없으면 새로운 pcb를 가져온다
    if (runningPcb == NULL) {
        dispatch();
		cout<<"혹시너1"<<endl;
    } else {
		cout<<"혹시너2"<<endl;
        // 현재 pcb의 CPU 버스트 시간 감소
        runningPcb->remainCpuBurst--;
		cout<<"혹시너3"<<endl;
        runningPcbRunTick++;
		cout<<"혹시너4"<<endl;

		if(runningPcb->ioStartTick>0){
			runningPcb->ioStartTick--;
			if(runningPcb->ioStartTick==0){//io시작할 시간 되면
				ioQueue.push(runningPcb);//io큐에 들어가라
				dispatch();//새로운 프로세스 꺼내라
			}
		}
		cout<<"혹시너5"<<endl;


        // CPU 버스트가 완료되면, 프로세스 종료 처리
        if (runningPcb!=NULL && runningPcb->remainCpuBurst <= 0) {
			cout<<"혹시너6"<<endl;
            Msg msg;
            msg.type = runningPcb->pid;
            msg.content = TERMINATED;
            msgsnd(msgQueId, &msg, sizeof(Msg), IPC_NOWAIT);
			cout<<"혹시너7"<<endl;
            contextSwitch();
			cout<<"혹시너8"<<endl;
        }
        // 타임 슬라이스 초과 시, 다음 프로세스로 전환
        else if (runningPcb!=NULL && runningPcbRunTick >= timeslice) {
			cout<<"혹시너9"<<endl;
            contextSwitch();
        }
		cout<<"혹시너10"<<endl;
    }
	cout<<"혹시너11"<<endl;
	

    // 로그 출력 및 타임틱 증가
    printLog();
    timeTickPassed++;
}

//디스패치 함수
void dispatch() {
	if(!readyQueue.empty()) {
		//맨 앞의 pcb를 꺼내 현재 실행중인 pcb로 설정
		runningPcb=readyQueue.front();
		readyQueue.pop();
		runningPcbRunTick=0;
		Msg msg;
		msg.type=runningPcb->pid;
		msg.content=START;
		//꺼낸 pcb의 pid와 동일한 자식프로세스에게 너 실행해 라는 메시지 전송
		msgsnd(msgQueId, &msg, sizeof(Msg), 0);

		//만약 새로 꺼낸 pcb가 처음으로 running상태가 된거라면 firstruntime기록
		if(runningPcb->firstRun == true){
			schedulingMetricMap[runningPcb->pid].arrivalTick=timeTickPassed;
			runningPcb->firstRun=false;
		}

		Msg msgFromChild;
		
		//자식에게서 온 메시지가 1(io작업 할거야)이면
		cout<<"waiting doIo info from child"<<endl;
		if(msgrcv(msgQueId, &msgFromChild, 50,parentPid,0)>0){
			runningPcb->doIo=1;
			if(msgFromChild.content==1){
				//cout<<"getMsg!"<<endl;

				Msg ioInfoFromChild;

				cout<<"waiting Io info from child"<<endl;
				msgrcv(msgQueId, &ioInfoFromChild, 50,parentPid,0);
				cout<<"received ioInfo: "<<ioInfoFromChild.content<<endl;
				//자식이 보낸 ioStartTick정보(십의 자리) 가져와 pcb에 저장
				runningPcb->ioStartTick=ioInfoFromChild.content/10;
				cout<<"pcb ioStartTick: "<<runningPcb->ioStartTick<<endl;
				runningPcb->remainIoBurst=ioInfoFromChild.content%10;
				cout<<"pcb remainIoBurst: "<<runningPcb->remainIoBurst<<endl;

			}
		}

		
		

	}else{
    runningPcb=NULL;
  	}
}

void contextSwitch() {
	//현재 실행중인 pcb의 remainCpuBurst가 남아있다면 ready큐의 맨 뒤에 넣어야함
	if(runningPcb!=NULL && runningPcb->remainCpuBurst >0) {
		readyQueue.push(runningPcb);
	}
	dispatch();
}

void clearMessageQueue(int msgid) {
    Msg msg;
    while (msgrcv(msgid, &msg, sizeof(msg.content), 0, IPC_NOWAIT) != -1) {
		std::cout << "remained message : " << msg.content << endl;
	 }

    // if (errno != ENOMSG) {
    //     std::cerr << ERROR_LOG_PREFIX << "Error in clearing Msg queue: " << strerror(errno) << std::endl;
    // }
}
int main() {
	srand(time(NULL));
	clearMessageQueue(msgQueId);
	
	parentPid=getpid();

	//키가 1398인 메시지큐 생성. msgQueId가 메시지큐를 식별할 수 있는 ID역할
	msgQueId = msgget(6666, IPC_CREAT | 0666);
	if(msgQueId==-1) {
		cout<<"msgget error"<<endl;
	}
	//타임 틱 설정
	struct itimerval timer;
	memset(&timer, 0, sizeof(timer));
	// 0.3초 설정 (300000 마이크로초)
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 300000;
	// 타이머 시작 시간
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 300000;
	// 타이머 반복 간격
	// SIGALRM 핸들러 등록
	// SIGALRM: 이 신호는 setitimer() 함수로 예약한 타이머가 만료되었을 때 발생
	// 내가 설정한 타이머 시간 단위마다 perTimeTick 함수가 동작한다
	signal(SIGALRM, perTimeTick);
	cout<<"=========== OS Term Project ==========="<<endl<<endl;
	cout<<"Time Slice: "<<timeslice<<endl<<endl;


	//자식 프로세스들 생성 파트
	for (int i=0; i<CHILDPROCESSNUM;i++) {
		//프로세스 생성
		long newPid=fork();

		//부모 프로세스에서 코드
		if (newPid > 0) {
			Pcb* pcbP= new Pcb;
			pcbP->pid=newPid;
			// cpuburst의 범위는 1~10
			pcbP->remainCpuBurst=rand()%10 +1;
			readyQueue.push(pcbP);
			ProcessTimeInfo processTimeInfo;
			schedulingMetricMap[newPid]=processTimeInfo;
		}		

		//자식 프로세스에서 코드 
		else if (newPid == 0) {
		bool runBefore=false;
				long myPid=getpid();
				int doIo, ioStartTick, remainIoBurst;
				Msg msg;
				msg.type = myPid;
				while(1) {
					//메시지큐에 가져올 메시지가 있을때까지 대기
					cout<<"waiting state info from parent"<<endl;
					msgrcv(msgQueId, &msg, 50, myPid,0);
					cout<<"received state info from parent"<<endl;
					//cout<<"msg content:"<<msg.content<<endl;
					//현재 start하라는 메시지였으면
					
					if (msg.content==START) {
						Msg ioInfoToParent;
						ioInfoToParent.type=parentPid;
						//io작업 할까 말까
						doIo=rand()%10;
						//doIo=7;

						//만약 io작업 하면
						if(doIo>=5) {//확률 0.5
							cout<<"do IO"<<endl;

							ioInfoToParent.content=1;
							msgsnd(msgQueId, &ioInfoToParent, sizeof(Msg),0);
						
							cout<<"send do io to parent"<<endl;	

							//timeslice내(1~timeslice-1)에서 시작시간 지정 
							ioStartTick = rand()%(timeslice-2) +1;
							cout<<"make iostartTick: "<<ioStartTick<<endl;

							//1~10내로 io지속시간 설정
							remainIoBurst = rand()%10 +1;
							cout<<"make remainIoBurst: "<<remainIoBurst<<endl;

							//io 시작시간 보내준다

							ioInfoToParent.content=ioStartTick*10+remainIoBurst;
							msgsnd(msgQueId, &ioInfoToParent, sizeof(Msg),0);
							cout<<"send ioinfo to parent"<<endl;	
							
							
						}else{
							msg.content=0;
							msgsnd(msgQueId, &msg, sizeof(Msg),0);
						}
						
					}
					//현재 pcb상태가 terminated하라는 메시지였으면
					else if(msg.content==TERMINATED) {
						exit(0);
					}
				}
			} 
		else if (newPid < 0) {
			printf("fork 실패!\n");
			exit(1);
		}
	}
	// //0번쨰 타임 틱에는 readyQueue의 첫 번째 pcb뽑아서 넣는다
	// runningPcb=readyQueue.front();
	// readyQueue.pop();
	// 타이머 설정
	// alarm함수는 초 단위로만 타이머를 설정할 수 있음. 
	// 분 수초 타이머 설정하려면 setitimer함수 사용해야함
	setitimer(ITIMER_REAL, &timer, NULL);

  //자식 프로세스 모두 종료될 때까지 기다린다
	for (int i = 0; i < CHILDPROCESSNUM; i++) {
    wait(NULL); // 여기서 NULL은 종료 상태를 받지 않겠다는 의미입니다.
  	}

  	cout<<"자식프로세스 모두 종료"<<endl;

  	for(auto info:schedulingMetricMap){
    	cout<<"arrivalTime:"<<info.second.arrivalTick<<endl;
	}
	cout<<"totalTimeTick: "<<timeTickPassed-1<<endl;
	return 0;
}