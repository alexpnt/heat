//
// 	Program for calculating the heat variation on a surface along the time.
//	*** Parallel version ***
//
//	Created by:
//	Alexandre Rui Santos Fonseca Pinto 
//	Carlos Miguel Rosa Avim
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <semaphore.h>
#include <signal.h>

#define TOTAL_STEPS 100			// the number of TOTAL_STEPS must be divisible by PROCESSES 
#define SQUARE_DIM 2000
#define SIZE 50000000
#define PROCESS 4
#define DATAFILE "temperature2000.csv"

//#define DEBUG	//remove this line to remove debug messages

void init();
void terminate();
void reinit();
void master();
void worker();
void read_file(FILE*);
void heat_calculating(int, int);
void write_file();
static void handler_c(int);
static void handler_worker(int);

typedef struct {
	sem_t barrier;
	sem_t mutex_line;
	sem_t mutex_finished;
	sem_t mutex_master;
	int step;
	int finished;
	int line;
	int contador;
	float matrix[SQUARE_DIM][SQUARE_DIM];
	float new_matrix[SQUARE_DIM][SQUARE_DIM];
} mem_struct;									//structure that will be shared in memory

int shmid;
float quarter=0;
mem_struct *sh_mem;
sem_t *barrier, *mutex_line, *mutex_finished, *mutex_master;


void init() {					//initialize semaphores, variables and create the shared memory

	if ( (shmid = shmget(IPC_PRIVATE, sizeof(mem_struct), IPC_CREAT|0700)) < 1 ) {
		perror("Error in shmget");
		terminate();
		exit(1);
	}
	
	if( (sh_mem = (mem_struct*) shmat(shmid, NULL, 0)) < (mem_struct*) 1) {
		perror("Error in shmat");
		terminate();
		exit(1);
	}
	
	if ( sem_init(&sh_mem->barrier, 1, PROCESS) == -1) {
		perror("Error creating barrier");
		terminate();
		exit(1);
	}
	barrier = &sh_mem->barrier;

	if ( sem_init(&sh_mem->mutex_line, 1, 1) == -1) {
		perror("Error creating mutex_line");
		terminate();
		exit(1);
	}
	mutex_line = &sh_mem->mutex_line;
	
	if ( sem_init(&sh_mem->mutex_finished, 1, 1) == -1) {
		perror("Error creating mutex_finished");
		terminate();
		exit(1);
	}
	mutex_finished = &sh_mem->mutex_finished;
	
	if ( sem_init(&sh_mem->mutex_master, 1, 0) == -1) {
		perror("Error creating mutex_master");
		terminate();
		exit(1);
	}
	mutex_master = &sh_mem->mutex_master;

	sh_mem->step = 0;
	sh_mem->finished = 0;
	sh_mem->line = 0;
	sh_mem->contador = 0;
}


void terminate() {			//clean shared memory and remove semaphors
	
#ifdef DEBUG
	printf("Removing semaphores and cleaning shared memory...\n");
#endif
	
	if( (shmctl(shmid, IPC_RMID, NULL) == -1) ) {
		perror("Error removing shared memory");
		exit(1);
	}
	if( (sem_destroy(barrier) == -1) ) {
		perror("Error destroying barrier");
		exit(1);
	}
	if( (sem_destroy(mutex_line) == -1) ) {
		perror("Error destroying mutex_line");
		exit(1);
	}
	if( (sem_destroy(mutex_finished) == -1) ) {
		perror("Error destroying mutex_finished");
		exit(1);
	}
	if( (sem_destroy(mutex_master) == -1) ) {
		perror("Error destroying mutex_master");
		exit(1);
	}
}


void reinit() {			//reset variables and reset the barrier value to the number of processes
	
	int i;
	
#ifdef DEBUG
	printf("A reiniciar valores...\n");
#endif
	
	sh_mem->finished = 0;
	sh_mem->line = 0;
	sh_mem->contador = 0;
	
	for (i=0; i<PROCESS; i++)
		sem_post(barrier);
}


int main(void)
{
	FILE *fp;
	int i;
	pid_t id;

	/*open file for reading*/
	if ((fp = fopen(DATAFILE, "r")) == NULL) {
		perror("Error opening file");
		exit(1);
	}
	
	init();

	/*read the contents of the file into a matrix*/
	read_file(fp);

	//fork process
	for(i=0;i<PROCESS;i++) {
		if ( (id = fork()) == -1) {
			perror("Failed to fork");
			exit(1);
		}
		if (id == 0) {
			signal(SIGINT, handler_worker);
			worker();
			exit(0);
		}
	}
	
	//handles sigint
	signal(SIGINT, handler_c);
	
	master();

	fclose(fp);

	for (i=0; i<PROCESS; i++)
		wait(NULL);
	
	terminate();
	
	return 0;
}

//sigint handler
static void handler_worker(int signum) {
	
	signal(SIGINT, handler_worker);

	exit(0);
}

static void handler_c(int signum) {
	
	sigset_t sigset;
	
#ifdef DEBUG
	printf("\nexecuting SIGINT handler...\n");
#endif
	
	signal(SIGINT, handler_c);
	
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);

	if (sigprocmask(SIG_BLOCK, &sigset, NULL) == -1) {
		perror("Error in sigprocmask");
		exit(1);
	}
	
	if(sigprocmask(SIG_UNBLOCK,&sigset,NULL) == -1) {
		perror("Error in sigprocmask");
		exit(1);
	}
		terminate();

		exit(0);
}


void master()
{
	int i;
	
	for(i=0; i<4; i++) {
		sem_wait(mutex_master);
	
		quarter+=0.25*TOTAL_STEPS;
		write_file();
		
		reinit();
	}
}


void worker()
{
	int line_start, line_end;
	
#ifdef DEBUG
	printf("worker running...\n");
#endif
	//execute synchronization algorithm when acessing to shared resources
	while (sh_mem->step < TOTAL_STEPS) {
		sem_wait(barrier);
		sem_wait(mutex_line);
		
			sh_mem->contador++;
			line_start = sh_mem->line;						//line_start and line_end-> beginning and end of the region
			sh_mem->line+= SQUARE_DIM/PROCESS;
			line_end = sh_mem->line;
			if(sh_mem->contador==1)
				line_start+=1;
			else if (sh_mem->contador==PROCESS)
				line_end-=1;
			
		sem_post(mutex_line);

		heat_calculating(line_start, line_end);
		
		sem_wait(mutex_finished);
			sh_mem->finished++;
			if (sh_mem->finished == PROCESS) {
				sh_mem->step++;
				if (sh_mem->step%(int)(TOTAL_STEPS*0.25) == 0)
					sem_post(mutex_master);
			
				else
					reinit();
			}
		sem_post(mutex_finished);
	}
}

//perform the calculation of the temperatures using the heat equation, in a given region of the matrix fixed by line_start and line_end
void heat_calculating(int line_start, int line_end)
{
	float cx = 0.1, cy = 0.1;
	int i, y;
#ifdef DEBUG
	printf("step ------> %d\n", sh_mem->step+1);
#endif
	for (i=line_start; i<line_end; i++) {
		for(y=1; y<SQUARE_DIM-1; y++) {
			if (sh_mem->step%2==0) {
				sh_mem->new_matrix[i][y] = sh_mem->matrix[i][y] + 
				cx*(sh_mem->matrix[i][y+1] + sh_mem->matrix[i][y-1] - 2*(sh_mem->matrix[i][y])) +
				cy*(sh_mem->matrix[i+1][y] + sh_mem->matrix[i-1][y] - 2*(sh_mem->matrix[i][y]));
			}
			
			else {
				sh_mem->matrix[i][y] = sh_mem->new_matrix[i][y] + 
				cx*(sh_mem->new_matrix[i][y+1] + sh_mem->new_matrix[i][y-1] - 2*(sh_mem->new_matrix[i][y])) +
				cy*(sh_mem->new_matrix[i+1][y] + sh_mem->new_matrix[i-1][y] - 2*(sh_mem->new_matrix[i][y]));
			}
		}
	}
}


void read_file(FILE *fp)
{
	int x,y;
	
#ifdef DEBUG
	printf("Reading file ...\n");
#endif
	
	for(x=0; x<SQUARE_DIM; x++)
	{
		for (y=0; y<SQUARE_DIM-1; y++) {
			if(fscanf(fp, "%f,", &sh_mem->matrix[x][y])==0)
				perror("Failed to read from file");
		}
		if(fscanf(fp, "%f", &sh_mem->matrix[x][y])==0)
			perror("Failed to read from file");
	}	
}

//write to disk
void write_file()
{
	int x, y;
	char filename[50];
	int fp;
	char *addr,*addr_copy,aux[20];
	
	sprintf(filename,"temperature2000_%g.csv",quarter);
	
	printf("Writing: %s\n", filename);
	
	//create output file
	if((fp=open(filename, O_RDWR|O_CREAT|O_TRUNC,0700))<0){
		perror("Error creating file");
		exit(1);
	}
	
	//alocate space for the created file
	if(lseek(fp, SIZE-1, SEEK_SET)==-1){
		perror("Seek Error");
	}
	
	if(write(fp, "", 1)<0)
		perror("Failed to allocate space");
	
	//map the output file into memory
	if ((addr = (char*) mmap(NULL, SIZE,PROT_WRITE, MAP_SHARED,fp, 0)) == MAP_FAILED){
		perror("Failed to map the file");
	}
	addr_copy=addr;
	
	//write matrix into the output file
	for(x=0; x<SQUARE_DIM; x++) {
		for(y=0; y<SQUARE_DIM-1; y++) {
			if (sh_mem->step%2==0)
				sprintf(aux, "%f,", sh_mem->new_matrix[x][y]);
			else
				sprintf(aux, "%f,", sh_mem->matrix[x][y]);
					
			sprintf(addr, "%s", aux); 
			addr+=strlen(aux);
		}
		if (sh_mem->step%2==0)
			sprintf(aux, "%f\n", sh_mem->new_matrix[x][y]);
		else
			sprintf(aux, "%f\n", sh_mem->matrix[x][y]);
		sprintf(addr, "%s", aux); 
		addr+=strlen(aux);
	}

	//unmap file
	if ((munmap(addr_copy, SIZE)) != 0) {
		perror("Error closing mapped file");
		exit(1);
	}	
}

