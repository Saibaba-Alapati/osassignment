#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>

struct node
{
    int val;
    struct node *next;
};

struct node *head = NULL;
struct node *tail = NULL;

void enque(int val)
{
    if (head == NULL)
    {
        head = (struct node *)malloc(sizeof(struct node));
        head->val = val;
        head->next = NULL;
        tail = head;
    }
    else
    {
        tail->next = (struct node *)malloc(sizeof(struct node));
        tail = tail->next;
        tail->val = val;
        tail->next = NULL;
    }
}

int deque()
{
    if (head == NULL)
        return -1;
    else
    {
        int val = head->val;
        head = head->next;
        return val;
    }
}

//these global variables are used for communication between main threads of all child processes and their corresponding task threads
int lock1 = 0, lock2 = 0, lock3 = 0; //these locks are used to stop or resume the task thread by main thread when main thread get a signal from parent
int ret1 = 0, ret2 = 0, ret3 = 0;    //these variables are used by task threads to tell main thread whether it is completed or not
long sum1, sum3;

//task of C1
void *runner1(void *n1)
{
    // here we have to malloc i think
    while (!lock1)
        ;
    int i = 1;
    long sum = 0;
    while (i <= (int)n1)
    {
        while (!lock1)
            ;
        sum += rand() % (1000000) + 1;
        i += 1;
    }
    // int result = (int)malloc(sizeof(int));
    // *result = sum;
    sum1 = sum;
    ret1 = 1; //to tell main thread that it is completed
    return NULL;
}

//task of C2
void *runner2(void *n2)
{
    clock_t s, e;
    double BT = 0;
    while (!lock2)
        ;
    s = clock();
    FILE *in_file;
    in_file = fopen("numbers.txt", "r");
    int i = 1, x;
    while (i <= (int)n2)
    {
        e = clock();
        BT += (double)(e - s) / CLOCKS_PER_SEC;
        while (!lock2)
            ;
        s = clock();
        fscanf(in_file, "%d", &x);
        printf("%d\n", x);
        i += 1;
    }
    ret2 = 1; //to tell main thread that it is completed
    e = clock();
    BT += (double)(e - s) / CLOCKS_PER_SEC;
    fclose(in_file);
    printf("Burst Time of process 2 is %lf\n", BT);
    return NULL;
}

//task of C3
void *runner3(void *n3)
{
    while (!lock3)
        ;
    FILE *in_file;
    in_file = fopen("numbers.txt", "r");
    int i = 1, x;
    long sum = 0;
    while (i <= (int)n3)
    {
        while (!lock3)
            ;
        fscanf(in_file, "%d", &x);
        sum += x;
        i += 1;
    }
    fclose(in_file);
    sum3 = sum;
    ret3 = 1; //to tell main thread that it is completed
    return NULL;
}

int main()
{

    int n1, n2, n3;
    char process[20];
    double timeQuantum;
    printf("Enter Scheduling Type:");
    scanf("%s", process);
    if (strcmp(process, "roundrobin"))
    {
        printf("Value Of Time Quantum: \n");
        scanf("%lf", &timeQuantum);
    }
    printf("Value n1: \n");
    scanf("%d", &n1);
    printf("Value n2: \n");
    scanf("%d", &n2);
    printf("Value n3: \n");
    scanf("%d", &n3);

    int pfd1[2], pfd2[2], pfd3[2];
    if (pipe(pfd1) < 0)
        exit(0);
    if (pipe(pfd2) < 0)
        exit(0);
    if (pipe(pfd3) < 0)
        exit(0);
    //forking first child C1
    int pid1 = fork();

    if (pid1 < 0)
        perror("Child Process-1 is not created");
    else if (pid1 == 0)
    {
        //child1 enters here
        close(pfd1[0]);

        int c1, shmid1, shmretid1, *sum;
        int *shm1, *shmret1;

        //shared memory to tell the parent whether the computation task is completed or not
        shmretid1 = shmget(2011, 32, 0644 | IPC_CREAT);
        if (shmretid1 == -1)
            exit(0);
        shmret1 = (int *)shmat(shmretid1, NULL, 0);
        *shmret1 = 0;

        sleep(2);
        //retrieving the shared memory id of the memory which will be used to communicate with parent and know whether to pause or execute the task
        shmid1 = shmget(2021, 32, 0);
        if (shmid1 == -1)
            exit(0);
        shm1 = (int *)shmat(shmid1, NULL, 0);

        //task thread creation
        pthread_t tid1;
        pthread_attr_t attr1;
        pthread_attr_init(&attr1);
        pthread_create(&tid1, &attr1, runner1, &n1);

        //these nested loops keeps checking the value of shm1 and pause or wake up the task thread accordingly
        while (1)
        {
            //this while loop make sure that it will not set lock1 value to 1 until the parent sets shm1 = 1
            //initial value of lock1 is 0
            // if lock1 = 0 task thread will be paused otherwise it will be executed
            while (!*shm1)
                ;

            lock1 = 1; //task thread is wake up

            //this while loop will not pause the task thread(set lock1 = 0) as long as shm1 = 1 and task thread is not completed(ret1 = 0)
            while (*shm1 && ret1 == 0)
                ;

            lock1 = 0; //task thread is paused

            //if task thread is completed it will set shmret1 = 1 so that parent will know that process is completed
            if (ret1 == 1)
            {
                //write to shared mem ret
                *shmret1 = 1;
                break;
            }
        }
        //writing the computed sum to parent
        write(pfd1[1], &sum1, sizeof(sum1));
        close(pfd1[1]);
        sleep(2); //just to make sure the main thread ends after task thread
    }
    else
    {
        //forking second child C2
        int pid2 = fork();

        if (pid2 < 0)
            perror("Child Process-2 is not created");
        else if (pid2 == 0)
        {
            //child2 enters here
            close(pfd2[0]);
            int c2, shmid2, shmretid2;
            int *shm2, *shmret2;
            //shared memory to tell the parent whether the computation task is completed or not
            shmretid2 = shmget(2012, 32, 0644 | IPC_CREAT);
            if (shmretid2 == -1)
                exit(0);
            shmret2 = (int *)shmat(shmretid2, NULL, 0);
            *shmret2 = 0;

            sleep(2);
            //retrieving the shared memory id of the memory which will be used to communicate with parent and know whether to pause or execute the task
            shmid2 = shmget(2022, 32, 0);
            if (shmid2 == -1)
                exit(0);
            shm2 = (int *)shmat(shmid2, NULL, 0);

            //task thread creation
            pthread_t tid2;
            pthread_attr_t attr2;
            pthread_attr_init(&attr2);
            pthread_create(&tid2, &attr2, runner2, &n2);

            //these nested loops keeps checking the value of shm2 and pause or wake up the task thread accordingly
            while (1)
            {
                //this while loop make sure that it will not set lock2 value to be 1 until the parent sets shm2 = 1
                //initial value of lock2 is 0
                // if lock2 = 0 task thread will be paused otherwise it will be executed
                while (!*shm2)
                    ;

                lock2 = 1; //task thread is wake up

                //this while loop will not pause the task thread(set lock2 = 0) as long as shm2 = 1 and task thread is not completed(ret2 = 0)
                while (*shm2 && ret2 == 0)
                    ;

                lock2 = 0; //task thread is paused

                //if task thread is completed it will set shmret2 = 1 so that parent will know that process is completed
                if (ret2 == 1)
                {
                    //write to shared mem ret
                    *shmret2 = 1;
                    break;
                }
            }
            //sending completed message to parent
            char *s = "Done Printing\n";
            write(pfd2[1], s, 14);
            close(pfd2[1]);
            sleep(2); //just to make sure the main thread ends after task thread
        }
        else
        {
            //forking third child C3
            int pid3 = fork();

            if (pid3 < 0)
                perror("Child Process-3 is not created");
            else if (pid3 == 0)
            {
                //child3 enters here
                close(pfd3[0]);

                int c3, shmid3, shmretid3;
                int *shm3, *shmret3;
                //shared memory to tell the parent whether the computation task is completed or not
                shmretid3 = shmget(2013, 32, 0644 | IPC_CREAT);
                if (shmretid3 == -1)
                    exit(0);
                shmret3 = (int *)shmat(shmretid3, NULL, 0);
                *shmret3 = 0;

                sleep(2);
                //retrieving the shared memory id of the memory which will be used to communicate with parent and know whether to pause or execute the task
                shmid3 = shmget(2023, 32, 0);
                if (shmid3 == -1)
                    exit(0);
                shm3 = (int *)shmat(shmid3, NULL, 0);

                //task thread creation
                pthread_t tid3;
                pthread_attr_t attr3;
                pthread_attr_init(&attr3);
                pthread_create(&tid3, &attr3, runner3, &n3);

                //these nested loops keeps checking the value of shm3 and pause or wake up the task thread accordingly
                while (1)
                {
                    //this while loop make sure that it will not set lock3 value to be 1 until the parent sets shm3 = 1
                    //initial value of lock3 is 0
                    // if lock3 = 0 task thread will be paused otherwise it will be executed.
                    while (!*shm3)
                        ;

                    lock3 = 1; //task thread is wake up

                    //this while loop will not pause the task thread(set lock3 = 0) as long as shm3 = 1 and task thread is not completed(ret3 = 0)
                    while (*shm3 && ret3 == 0)
                        ;

                    lock3 = 0; //task thread is paused

                    //if task thread is completed it will set shmret3 = 1 so that parent will know that process is completed
                    if (ret3 == 1)
                    {
                        //write to shared mem ret
                        *shmret3 = 1;
                        break;
                    }
                }
                //sending computed sum to parent
                write(pfd3[1], &sum3, sizeof(sum3));
                close(pfd3[1]);
                sleep(2); //just to make sure the main thread ends after task thread
            }
            else
            {
                //parent enters here
                close(pfd1[1]);
                close(pfd3[1]);
                close(pfd2[1]);
                double BurstTime[3] = {0, 0, 0};
                clock_t startTime = 0;
                double TurnAroundTime[3] = {0, 0, 0};
                double waitingTime[3] = {0, 0, 0};
                int shmid1, shmid2, shmid3, shmretid1, shmretid2, shmretid3;
                int *shm1, *shm2, *shm3, *shmret1, *shmret2, *shmret3;
                clock_t start, end;
                double diff_t;

                //Here one shared memories are created for each of three processes to store an integer that will tell the particular process to execute or pause the task thead
                shmid1 = shmget(2021, 32, 0644 | IPC_CREAT);
                if (shmid1 == -1)
                    exit(0);
                shm1 = (int *)shmat(shmid1, NULL, 0);
                *shm1 = 0;

                shmid2 = shmget(2022, 32, 0644 | IPC_CREAT);
                if (shmid2 == -1)
                    exit(0);
                shm2 = (int *)shmat(shmid2, NULL, 0);
                *shm2 = 0;

                shmid3 = shmget(2023, 32, 0644 | IPC_CREAT);
                if (shmid3 == -1)
                    exit(0);
                shm3 = (int *)shmat(shmid3, NULL, 0);
                *shm3 = 0;

                sleep(2);

                //here three shared memory variables are retrieved through which the main thread of each child process tell the parent whether a task thread is completed or not
                shmretid1 = shmget(2011, 32, 0);
                if (shmretid1 == -1)
                    exit(0);
                shmret1 = (int *)shmat(shmretid1, NULL, 0);

                shmretid2 = shmget(2012, 32, 0);
                if (shmretid2 == -1)
                    exit(0);
                shmret2 = (int *)shmat(shmretid2, NULL, 0);

                shmretid3 = shmget(2013, 32, 0);
                if (shmretid3 == -1)
                    exit(0);
                shmret3 = (int *)shmat(shmretid3, NULL, 0);

                //creating a queue [1,2,3] 1-c1, 2-c2, 3-c3 this is order in which processes started.
                enque(1);
                enque(2);
                enque(3);

                while (head != NULL)
                {
                    //taking out the first process in the queue
                    int p = deque();
                    int *shm; //pointer to control task thread execution
                    int *ret; //pointer to know completion of task threads

                    //we are storing the poniters for shared memory corresponding to the process we selected into some variables
                    if (p == 1)
                    {
                        shm = shm1;
                        ret = shmret1;
                    }
                    else if (p == 2)
                    {
                        shm = shm2;
                        ret = shmret2;
                    }
                    else
                    {
                        shm = shm3;
                        ret = shmret3;
                    }
                    start = clock(); //start time when we enabled process p
                    *shm = 1;        //enabling process p to execute
                    printf("\nproces %d started at %lf\n", p, (double)start / CLOCKS_PER_SEC);

                    if (startTime == 0)
                        startTime = start;
                    //this while loop keeps checking the difference between current time start time and it exceeds certain threshhold
                    //it will break and set shm = 0 so that task thread corresponding to process p will be paused
                    //it will also break if the task of process p is completed(ret = 1)
                    if (strcmp(process,"roundrobin"))
                    {
                        //RR with time quantum
                        while (((double)(clock() - start) / CLOCKS_PER_SEC) <= timeQuantum && *ret == 0)
                            ;
                        *shm = 0; //pausing task thread of process p
                        end = clock();
                        if (*ret == 0)
                            printf("\nprocess %d is paused at %lf\n", p, (double)end / CLOCKS_PER_SEC);
                        BurstTime[p - 1] += (double)(end - start) / CLOCKS_PER_SEC;
                    }
                    else
                    {
                        //FCFS
                        while (*ret == 0)
                            ;
                        *shm = 0; //pausing task thread of process p
                        end = clock();
                        BurstTime[p - 1] += (double)(end - start) / CLOCKS_PER_SEC;
                    }

                    //if the task of p is not completed in the given time we will put that process at the end of the queue(RR)
                    if (*ret == 0)
                        enque(p);
                    else
                    {
                        //if *ret = 1 which means the process p is completed we have retrieve the values sent by the child processes.
                        long res;
                        char buf[14];
                        TurnAroundTime[p - 1] = (double)(end - startTime) / CLOCKS_PER_SEC;
                        waitingTime[p - 1] = TurnAroundTime[p - 1] - BurstTime[p - 1];
                        if (p == 1)
                        {
                            read(pfd1[0], &res, sizeof(int));
                            close(pfd1[0]);
                        }
                        else if (p == 3)
                        {
                            read(pfd3[0], &res, sizeof(int));
                            close(pfd3[0]);
                        }
                        else
                        {
                            read(pfd2[0], buf, 14);
                            printf("\nprocess %d is ended at %lf\n", p, (double)end / CLOCKS_PER_SEC);
                            printf("message from process 2: %s", buf);
                            printf("Turn Around Time of %d is %lf\n", p, TurnAroundTime[p - 1]);
                            printf("Waiting Time of %d is %lf\n", p, waitingTime[p - 1]);
                            continue;
                        }
                        printf("\nprocess %d is ended at %lf\n", p, (double)end / CLOCKS_PER_SEC);
                        printf("result from process %d is %ld \n", p, res);
                        printf("Burst Time of %d is %lf\n", p, BurstTime[p - 1]);
                        printf("Turn Around Time of %d is %lf\n", p, TurnAroundTime[p - 1]);
                        printf("Waiting Time of %d is %lf\n", p, waitingTime[p - 1]);
                    }
                }
                wait(NULL);
                wait(NULL);
                wait(NULL);

                //here we are removing the ID's corresponding to the shared memories so that we can use them again.
                shmctl(2011, IPC_RMID, NULL);
                shmctl(2012, IPC_RMID, NULL);
                shmctl(2013, IPC_RMID, NULL);
                shmctl(2021, IPC_RMID, NULL);
                shmctl(2022, IPC_RMID, NULL);
                shmctl(2023, IPC_RMID, NULL);
            }
        }
    }
}