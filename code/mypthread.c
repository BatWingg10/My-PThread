// File:	mypthread.c

// List all group members' names:
// iLab machine tested on:

#include "mypthread.h"
#include <ucontext.h>
#include <stdbool.h>

// INITAILIZE ALL YOUR VARIABLES HERE

#define MAX_STACK_SIZE 16384 // maximum size of the context stack
#define MAX_SIZE 15			 // max queue size
#define QUANTUM 2000		 // quantum

tCB *currentThread, *previousThread; // maintains current and previous thread
ll *runningQueue[MAX_SIZE];
ll *threadStock[MAX_SIZE];
ucontext_t cleanup; // responsible for garbage collection
sigset_t signal_set;
mySig sig;
struct itimerval currentTime, timerVal;

bool isMainInitialised;
bool isExecuting;
int threadCount = 0; // using it for threadId
int timeElapsed;

/* create a new thread */

// Auxiliary functions

void registerSignal()
{
	memset(&sig, 0, sizeof(mySig)); // memory set function of C
	sig.sa_handler = &scheduler;
	sigaction(SIGVTALRM, &sig, NULL);
}

void initializeQueue()
{
	for (int i = 0; i < MAX_SIZE; i++)
	{
		runningQueue[i] = NULL;
		threadStock[i] = NULL;
	}
}

tCB *removeFromQueue(ll **q)
{
	ll *queue = *q;

	if (queue == NULL)
	{
		return NULL;
	}

	ll *temp = queue;
	tCB *target = queue->thread;
	queue = queue->next;
	free(temp);
	*q = queue;
	return target;
}

void insertToQueue(ll **q, tCB *joinThread)
{
	ll *queue = *q;

	if (queue == NULL)
	{
		queue = (ll *)malloc(sizeof(ll));
		queue->thread = joinThread;
		queue->next = NULL;
		*q = queue;
		return;
	}

	ll *newNode = (ll *)malloc(sizeof(ll));
	newNode->thread = joinThread;

	newNode->next = queue;

	queue = newNode;
	*q = queue;
}

void enqueue(ll **q, tCB *thread)
{
	ll *queue = *q;

	if (queue == NULL)
	{
		queue = (ll *)malloc(sizeof(ll));
		queue->thread = thread;
		queue->next = queue;
		*q = queue;
		return;
	}

	ll *front = queue->next;
	queue->next = (ll *)malloc(sizeof(ll));
	queue->next->thread = thread;
	queue->next->next = front;

	queue = queue->next;
	*q = queue;
	return;
}

tCB* dequeue(ll** q)
{
	ll *queue = *q;
	if(queue == NULL)
	{
		return NULL;
	}
	//Basic Logic is that the queue is the last element in a queue at level i
	//Therefore, we first get the tcb to be returned
	
	ll *front = queue->next;
	tCB *target = queue->next->thread;
	
	//We also check whether there is only one element left in the queue
	//Appropriately assign null/free
	if(queue->next == queue)
	{
		queue = NULL;
	}
	else
	{
		queue->next = front->next;
	}
	free(front);


	if(target == NULL)
	{printf("ERROR IN DEQUEUE\n");}

	*q = queue;
	return target;
}

void collectGarbage()
{
	isExecuting = true;

	currentThread->status = 4;

	// Check condition for pthread creation
	if (!isMainInitialised)
	{
		exit(EXIT_SUCCESS);
	}

	tCB *relatedThread = NULL; // any related threads waiting on the one being trashed

	// We have to dequeue all the threads waiting current thread to finish
	while (currentThread->joinQueue != NULL)
	{
		relatedThread = removeFromQueue(&currentThread->joinQueue);
		relatedThread->retVal = currentThread->jVal;
		enqueue(&runningQueue[relatedThread->priority], relatedThread);
	}

	// Empty the list
	int index = currentThread->threadID % MAX_SIZE;
	if (threadStock[index]->thread->threadID == currentThread->threadID)
	{
		ll *remList = threadStock[index];
		threadStock[index] = threadStock[index]->next;
		free(remList); // free the memory
	}
	else
	{
		ll *temp = threadStock[index];
		while (threadStock[index]->next != NULL)
		{
			if (threadStock[index]->next->thread->threadID == currentThread->threadID)
			{
				ll *remainingList = threadStock[index]->next;
				threadStock[index]->next = remainingList->next;
				free(remainingList);
				break;
			}
			threadStock[index] = threadStock[index]->next;
		}

		threadStock[index] = temp;
	}

	isExecuting = false; // Finished execution

	raise(SIGVTALRM); // Call the scheduler
}

void initializeGarbageContext() // Initialize garbage collector
{
	registerSignal();
	initializeQueue();

	getcontext(&cleanup);
	cleanup.uc_link = NULL;
	cleanup.uc_stack.ss_sp = malloc(MAX_STACK_SIZE);
	cleanup.uc_stack.ss_size = MAX_STACK_SIZE;
	cleanup.uc_stack.ss_flags = 0;
	makecontext(&cleanup, (void *)&collectGarbage, 0); // when cleanup resumes, garbage_collection will be called

	threadCount = 1; // start ThreadIDs from 1
}

void scheduler(int signum)
{
	if (isExecuting) // thread is not ready for scheduling
	{
		return;
	}

	// Record remaining time
	getitimer(ITIMER_VIRTUAL, &currentTime);

	// Check if active, reset timer to zero
	timerVal.it_value.tv_sec = 0;
	timerVal.it_value.tv_usec = 0;
	timerVal.it_interval.tv_sec = 0;
	timerVal.it_interval.tv_usec = 0;
	setitimer(ITIMER_VIRTUAL, &timerVal, NULL);

	if (signum != SIGVTALRM)
	{
		exit(signum);
	}

	// Calculate expected time interval
	int timeTaken = (int)currentTime.it_value.tv_usec;
	int expectedInterval = QUANTUM * (currentThread->priority + 1);

	if (timeTaken < 0 || timeTaken > expectedInterval)
	{
		timeTaken = 0;
	}
	else
	{
		int currentTimeSpent = expectedInterval - timeTaken;
		timeTaken = currentTimeSpent;
	}

	timeElapsed = timeTaken + timeElapsed;

	// Checking maintenance cycle
	if (timeElapsed >= 10000000)
	{
		maintenance();
		timeElapsed = 0; // reset counter
	}

	previousThread = currentThread;

	int i;

	switch (currentThread->status)
	{
	case 0: // READY

		if (currentThread->priority < MAX_SIZE - 1)
		{
			currentThread->priority++;
		}

		enqueue(&runningQueue[currentThread->priority], currentThread);

		currentThread = NULL;

		for (i = 0; i < MAX_SIZE; i++)
		{
			if (runningQueue[i] != NULL)
			{
				// Running a new thread
				currentThread = dequeue(&runningQueue[i]);
				break;
			}
		}

		if (currentThread == NULL)
		{
			currentThread = previousThread;
		}

		break;

	case 3: // YIELD

		currentThread = NULL;

		for (i = 0; i < MAX_SIZE; i++)
		{
			if (runningQueue[i] != NULL)
			{
				currentThread = dequeue(&runningQueue[i]);
				break;
			}
		}

		if (currentThread != NULL)
		{
			enqueue(&runningQueue[previousThread->priority], previousThread);
		}
		else
		{
			currentThread = previousThread;
		}

		break;

	case 1: // WAIT
		break;

	case 4: // EXIT

		currentThread = NULL;

		for (i = 0; i < MAX_SIZE; i++)
		{
			if (runningQueue[i] != NULL)
			{
				currentThread = dequeue(&runningQueue[i]);
				break;
			}
		}

		if (currentThread != NULL)
		{
			// free the TCB and u_context
			free(previousThread->context->uc_stack.ss_sp);
			free(previousThread->context);
			free(previousThread);

			currentThread->status = 0;

			// Reset the timer
			timerVal.it_value.tv_sec = 0;
			timerVal.it_value.tv_usec = QUANTUM * (currentThread->priority + 1);
			timerVal.it_interval.tv_sec = 0;
			timerVal.it_interval.tv_usec = 0;
			int ret = setitimer(ITIMER_VIRTUAL, &timerVal, NULL);
			if (ret < 0)
			{
				exit(0);
			}
			setcontext(currentThread->context);
		}

		break;

	case 2: // JOIN - corresponds to a call to pthread_join

	case 5: // MUTEX_LOCK - corresponds to thread waiting for a mutex lock

		currentThread = NULL;
		for (i = 0; i < MAX_SIZE; i++)
		{
			if (runningQueue[i] != NULL)
			{
				currentThread = dequeue(&runningQueue[i]);
				break;
			}
		}

		if (currentThread == NULL)
		{
			exit(EXIT_FAILURE);
		}

		break;

	default:
		// NOT USED

		exit(-1);
		break;
	}

	currentThread->status = 0;

	// reset timer to 20ms times thread priority
	timerVal.it_value.tv_sec = 0;
	timerVal.it_value.tv_usec = QUANTUM * (currentThread->priority + 1);
	timerVal.it_interval.tv_sec = 0;
	timerVal.it_interval.tv_usec = 0;
	int ret = setitimer(ITIMER_VIRTUAL, &timerVal, NULL);

	if (ret < 0)
	{
		exit(0);
	}

	// Switch to new context
	if (previousThread->threadID != currentThread->threadID)
	{
		swapcontext(previousThread->context, currentThread->context);
	}

	return;
}

void initializeMainContext()
{
	ucontext_t *mText = (ucontext_t *)malloc(sizeof(ucontext_t));
	getcontext(mText);
	mText->uc_link = &cleanup;

	tCB *baseThread = (tCB *)malloc(sizeof(tCB));
	baseThread->context = mText;
	baseThread->threadID = 0;
	baseThread->priority = 0;
	baseThread->status = 0;
	baseThread->joinQueue = NULL;
	baseThread->jVal = NULL;
	baseThread->retVal = NULL;

	insertToQueue(&threadStock[0], baseThread);

	currentThread = baseThread;

	isMainInitialised = true;
}

int mypthread_create(mypthread_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
	if (!isMainInitialised)
	{
		initializeGarbageContext();
	}

	bool isExecuting = 1;

	ucontext_t *threadContext = (ucontext_t *)malloc(sizeof(ucontext_t));
	getcontext(threadContext);

	if (&cleanup == NULL)
	{
		return -1;
	}
	threadContext->uc_link = &cleanup;

	threadContext->uc_stack.ss_sp = malloc(MAX_STACK_SIZE);
	threadContext->uc_stack.ss_size = MAX_STACK_SIZE;
	threadContext->uc_stack.ss_flags = 0;

	makecontext(threadContext, (void *)function, 1, arg);

	if (threadContext == NULL)
	{
		return -1;
	}

	tCB *newThread = (tCB *)malloc(sizeof(tCB));
	newThread->context = threadContext;
	newThread->threadID = threadCount;
	newThread->priority = 0;
	newThread->status = 0;
	newThread->joinQueue = NULL;
	newThread->jVal = NULL;
	newThread->retVal = NULL;

	*thread = threadCount;
	threadCount++;

	enqueue(&runningQueue[0], newThread);
	int index = newThread->threadID % MAX_SIZE;
	insertToQueue(&threadStock[index], newThread);

	isExecuting = false;

	// store main context

	if (!isMainInitialised)
	{
		initializeMainContext();

		raise(SIGVTALRM);
	}

	return 0;
};

/* current thread voluntarily surrenders its remaining runtime for other threads to use */
int mypthread_yield()
{

	if (!isMainInitialised)
	{
		initializeGarbageContext();
		initializeMainContext();
	}

	if (currentThread == NULL)
	{
		return -1;
	}

	// Return to signal handler/scheduler
	currentThread->status = 3;
	return raise(SIGVTALRM);

	// change current thread's state from Running to Ready
	// save context of this thread to its thread control block
	// switch from this thread's context to the scheduler's context

	return 0;
};

/* terminate a thread */
void mypthread_exit(void *value_ptr)
{
	if (!isMainInitialised)
	{
		initializeGarbageContext();
		initializeMainContext();
	}

	if (currentThread == NULL || &cleanup == NULL)
	{
		return;
	}

	currentThread->jVal = value_ptr;
	setcontext(&cleanup);

	// preserve the return value pointer if not NULL
	// deallocate any dynamic memory allocated when starting this thread

	return;
};

/* Wait for thread termination */
int mypthread_join(mypthread_t thread, void **value_ptr)
{
	if (!isMainInitialised)
	{
		initializeGarbageContext();
		initializeMainContext();
	}
	isExecuting = true;

	// Keep check that thread can't wait on self
	if (thread == currentThread->threadID)
	{
		return -1;
	}

	tCB *headThread = searchThreadById(thread);

	if (headThread == NULL)
	{
		return -1; // thread not present in allThreads
	}

	// Priority Inversion Case
	headThread->priority = 0;

	insertToQueue(&headThread->joinQueue, currentThread);

	currentThread->status = 2;

	isExecuting = false;
	raise(SIGVTALRM);

	if (value_ptr == NULL)
	{
		return 0;
	}

	*value_ptr = currentThread->retVal;

	return 0;

	// wait for a specific thread to terminate
	// deallocate any dynamic memory created by the joining thread
};

/* initialize the mutex lock */
int mypthread_mutex_init(mypthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
	isExecuting = true;
	mypthread_mutex_t m = *mutex;

	if (&m == NULL)
	{
		return -1;
	}

	m.available = 1;
	m.holder = -1;
	m.locked = 0;
	m.queue = NULL;

	*mutex = m;
	isExecuting = false;

	return 0;
};

/* aquire a mutex lock */
int mypthread_mutex_lock(mypthread_mutex_t *mutex)
{
	if (!isMainInitialised)
	{
		initializeGarbageContext();
		initializeMainContext();
	}
	isExecuting = true;

	if (mutex == NULL || !mutex->available) // check mutex availability
	{
		return -1;
	}

	while (__atomic_test_and_set((volatile void *)&mutex->locked, __ATOMIC_RELAXED)) 
	{
		isExecuting = true;
		enqueue(&mutex->queue, currentThread);
		currentThread->status = 5;

		isExecuting = false;
		raise(SIGVTALRM);
	}

	if (!mutex->available)
	{
		mutex->locked = 0;
		return -1;
	}

	// Priority Inversion Case
	currentThread->priority = 0;
	mutex->holder = currentThread->threadID;

	isExecuting = false;

	return 0;
};

/* release the mutex lock */
int mypthread_mutex_unlock(mypthread_mutex_t *mutex)
{
	if(!isMainInitialised)
	{
		initializeGarbageContext();
		initializeMainContext();
	}

	if(mutex == NULL || !mutex->available || !mutex->locked || mutex->holder != currentThread->threadID)
	{return -1;}

	isExecuting = true;

	mutex->locked = 0;
	mutex->holder = -1;

	tCB* muThread = dequeue(&mutex->queue);

	if(muThread == NULL)
	{
		return -1;
	}
	muThread->priority = 0;
    enqueue(&runningQueue[0], muThread);

	isExecuting = false;

	return 0;

	// update the mutex's metadata to indicate it is unlocked
	// put the thread at the front of this mutex's blocked/waiting queue in to the run queue

};

/* destroy the mutex */
int mypthread_mutex_destroy(mypthread_mutex_t *mutex)
{
	isExecuting = true;
	mypthread_mutex_t m = *mutex;

	if(&m == NULL)
    {
        return -1;
    }

	m.available = 0;
	isExecuting = false;

	while(m.locked)
	{raise(SIGVTALRM);}

	tCB *muThread;
	while(m.queue != NULL)        
	{
		muThread = dequeue(&m.queue);
		enqueue(&runningQueue[muThread->priority], muThread);
	}

	*mutex = m;

	// deallocate dynamic memory allocated during mypthread_mutex_init

	return 0;
};

/* scheduler */
static void schedule()
{
	
	#ifdef MLFQ
		memset(&sig,0,sizeof(mySig)); //memory set function of C
 		sig.sa_handler = &scheduler;
 		sigaction(SIGVTALRM, &sig,NULL);
 	#endif
 	#ifdef PSJF
		//allocate signal for psjf here
	#else
		memset(&sig,0,sizeof(mySig));
		sig.sa_handler = &sched_RR;
		sigaction(SIGVTALRM, &sig,NULL);
	#endif

	// be sure to check the SCHED definition to determine which scheduling algorithm you should run
	//   i.e. RR, PSJF or MLFQ

	return;
}

/* Round Robin scheduling algorithm */
static void sched_RR(int signum)
{
	
	if(isExecuting){
		return;
	}
	//Record remaining time
	getitimer(ITIMER_VIRTUAL, &currentTime);

	//Check if active, reset timer to zero
	timerVal.it_value.tv_sec = 0;
	timerVal.it_value.tv_usec = 0;
	timerVal.it_interval.tv_sec = 0;
	timerVal.it_interval.tv_usec = 0;
	setitimer(ITIMER_VIRTUAL, &timer, NULL);

	if(signum != SIGVTALRM)
	{
		exit(signum);
	}

	if(runningQueue[RRQUEUE] == NULL){
		exit(0);
	}

	previousThread = currentThread;
	switch (currentThread->status)
	{
	case 0:
		
	case 3:
		currThread = NULL;
		if(runningQueue[RRQUEUE]!=NULL){
			currThread = dequeue(&runningQueue[RRQUEUE]);
		}
		if(currThread!=NULL){
			enqueue(&runningQueue[RRQUEUE], prevThread);
		}else{
			currThread = prevThread;
		}
		break;
	case 4:

		currThread = NULL;
		if(runningQueue[RRQUEUE]!=NULL){
			currThread = dequeue(&runningQueue[RRQUEUE]);
		}
		if(currThread != NULL)
		{
			//free the TCB and u_context
			free(prevThread->context->uc_stack.ss_sp);
			free(prevThread->context);
			free(prevThread);

			currThread->status = READY;

			//Reset the timer
			timer.it_value.tv_sec = 0;
			timer.it_value.tv_usec = QUANTUM * (RRQUEUE);
			timer.it_interval.tv_sec = 0;
			timer.it_interval.tv_usec = 0;
			int ret = setitimer(ITIMER_VIRTUAL, &timer, NULL);
			if (ret < 0)
			{
				exit(0);
			}
			setcontext(currThread->context);
		}

		break;
	case 1:
	case 2:

	case 5:
		currThread = NULL;
		if(runningQueue[RRQUEUE]!=NULL){
			currThread = dequeue(&runningQueue[RRQUEUE]);
		}
		if(currThread == NULL)
		{
			exit(EXIT_FAILURE);
		}
		break;
	default:
		exit(-1);
		break;
	}

	currThread->status = READY;
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = QUANTUM;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;
	int ret = setitimer(ITIMER_VIRTUAL, &timer, NULL);
	if (ret < 0)
	{
		 exit(0);
	}
	if(prevThread->tid != currThread->tid)
	{swapcontext(prevThread->context, currThread->context);}
	return;
	
	// Your own implementation of RR
	// (feel free to modify arguments and return types)

	return;
}

/* Preemptive PSJF (STCF) scheduling algorithm */
static void sched_PSJF()
{
	// YOUR CODE HERE

	// Your own implementation of PSJF (STCF)
	// (feel free to modify arguments and return types)

	return;
}

/* Preemptive MLFQ scheduling algorithm */
/* Graduate Students Only */
static void sched_MLFQ()
{
	// YOUR CODE HERE

	// Your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	return;
}

// Feel free to add any other functions you need

// YOUR CODE HERE
