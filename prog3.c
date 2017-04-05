/********************************************************************
 * Program 3 : Whack-a-Mole
 * 
 * 8 Command Lines Args
 * ------------------
 * 1. X-Dimension of Game Board
 * 2. Y-Dimension of Game Board
 * 3. Total Number of Moles in the Game
 * 4. Max Number of Moles out at one time
 * 5. Minimum time a mole will hide
 * 6. Maximum time a mole will hide
 * 7. Minimum time a mole will be out
 * 8. Maximum time a mole will be out
 *
 * author: Mitch Couturier
 * date: 4/3/2017
 ********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h> 
#include <curses.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

/*Functions*/
void *mole(void *moleNum);		// represents a mole in the game
void *keyboardHandler();		// handles user input for hitting the mole or exiting the game
void draw();				// draws the game board on the screen using curses

/*Global Variables*/
pthread_t kh_thread;			// keyboard handler thread
int *moles;				// array of the status of each mole (0 = hidden, 1 = active, 2 = hit) 
char *keys;				// array of character containing the keys to press for each cell
int board_dimenX;			// X dimensions of board
int board_dimenY;			// Y dimensions of board
int moleTotal;				// total number of moles in the game
int moleLimit;				// total number of moles that can display at once
int hideTimeMin, hideTimeMax;		// min and max times that a mole can hide in milliseconds
int outTimeMin, outTimeMax;		// min and max times that a mole is out in milliseconds
char input;				// user input character
int hits, misses;			// counts for hits and misses on moles
int threadCount;			// count of currently running threads
int shouldExit;				// is (1) if the program needs to exit
int semId;				// semaphore set ([0] = mole semaphore, [1] = draw semaphore)
struct sembuf 	moleSemBufWait,
		moleSemBufSignal, 
		drawSemBufWait,
		drawSemBufSignal;	// contains operations for each semaphore


int main(int argc, char *argv[]){
	/* get command line args */
	if(argc != 9){
		fprintf(stderr,"ERROR: Not a valid number of arguments\n");
		exit(1);
	}
	board_dimenX = atoi(argv[1]);
	board_dimenY = atoi(argv[2]);
	moleTotal = atoi(argv[3]);
	moleLimit = atoi(argv[4]);
	hideTimeMin = atoi(argv[5]);
	hideTimeMax = atoi(argv[6]);
	outTimeMin = atoi(argv[7]);
	outTimeMax = atoi(argv[8]);

	/* check for impossibilites within arguments */
	if(board_dimenX*board_dimenY > 26){
		fprintf(stderr,"ERROR: Board dimensions too large. Max = 26\n");
		exit(1);
	}

	if(moleTotal > board_dimenX*board_dimenY){
		moleTotal = board_dimenX*board_dimenY;
	}

	/* initialize variables */
	pthread_t *mole_threads; 		// array of threads used for mole logic
	hits = 0;
	misses = 0;
	shouldExit = 0;
	srand(time(NULL));
	threadCount = 0;

	/* initialize semaphore set */
	semId = semget(IPC_PRIVATE, 2, 00600);
	semctl(semId, 0, SETVAL, moleLimit);
	semctl(semId, 1, SETVAL, 1);
	moleSemBufWait.sem_num = 0;
	moleSemBufWait.sem_op = -1;
	moleSemBufSignal.sem_num = 0;
	moleSemBufSignal.sem_op = 1;
	drawSemBufWait.sem_num = 1;
	drawSemBufWait.sem_op = -1;
	drawSemBufSignal.sem_num = 1;
	drawSemBufSignal.sem_op = 1;

	/* initialize curses */
	initscr();
	noecho();
	curs_set(0);

	/* allocate memory */
	if ((mole_threads= malloc(board_dimenX*board_dimenY * sizeof(pthread_t))) == 0) {
		perror("Cannot allocate memory. Out of space.");
		endwin();
		exit(1);
	}
	if ((moles= malloc(board_dimenX*board_dimenY * sizeof(int))) == 0) {
		perror("Cannot allocate memory. Out of space.");
		endwin();
		exit(1);
	}
	if ((keys= malloc(board_dimenX*board_dimenY * sizeof(char))) == 0) {
		perror("Cannot allocate memory. Out of space.");
		endwin();
		exit(1);
	}


	/* populate keys array */
	int keyVal = 97; //'a'
	int k;
	for(k=0; k < board_dimenX*board_dimenY; k++){
		keys[k] = keyVal;
		keyVal++;
	}

	/* create threads and moles */
	int i, status;
	if ((status = pthread_create(&kh_thread, NULL, keyboardHandler, NULL)) != 0) {
		fprintf(stderr, "thread create error %d: %s\n", status, strerror(status));
		endwin();
		exit(1);
	}
	threadCount++;
	for(i=0; i < moleTotal; i++){
		int *val = malloc(sizeof(*val));
		*val = i;

		if ((status = pthread_create(&mole_threads[i], NULL, mole, val)) != 0) {
			fprintf(stderr, "thread create error %d: %s\n", status, strerror(status));
			endwin();
			exit(1);
		}
		threadCount++;
	}

	/* wait for threads to join */
	int j;
	if ((status = pthread_join(kh_thread, NULL)) != 0) {
		fprintf(stderr, "join error %d:%s\n", status, strerror(status));
		endwin();
		exit(1);
	}
	for(j=0; j < moleTotal; j++){
		if ((status = pthread_join(mole_threads[j], NULL)) != 0) {
			fprintf(stderr, "join error %d:%s\n", status, strerror(status));
			endwin();
			exit(1);
		}
	}

	/* clean up and exit*/
	semctl(semId, 0, IPC_RMID);
	semctl(semId, 1, IPC_RMID);
	free(moles);
	free(keys);

	printw("\nCleanup successful!\n\nPress any key to exit.");
	refresh();
	draw();
	getch();
	
	endwin();
	return 0;
}

void *mole(void *moleNum){
	/* get mole number */
	int index = *((int *)moleNum);
	free(moleNum);

	//get random value (1) or (0)
	int active = rand() % 2;
	
	/* swap between hiding and active states */
	while(!shouldExit){

		// if active is (1), pop up mole
		if(active == 1){
			/**** entry section ****/
			semop(semId, &moleSemBufWait, 1);
			/***********************/
			//make mole active
			moles[index] = 1;
			draw();
			int randomTime = rand() % (outTimeMax + 1 - outTimeMin) + outTimeMin; // between outTimeMin ms and outTimeMax ms
			usleep(randomTime*1000);

			// reset active
			active = 0;
			
			/**** exit section ****/
			semop(semId, &moleSemBufSignal, 1);
			moles[index] = 0;
			/**********************/
			draw();
		}
		// if active is (0), sleep then set to (1)
		else {
			int randomTime = rand() % (hideTimeMax + 1 - hideTimeMin) + hideTimeMin; // between hideTimeMin ms and hideTimeMax ms
			usleep(randomTime*1000);
			active = 1;
		}
	}

	threadCount--;
	return 0;
}

void draw(){
	/**** entry section ****/
	semop(semId, &drawSemBufWait, 1);
	/***********************/

	int i,j;
	move(1,0);
	printw("\tMitch's Moles!");
	move(2,0);
	printw("_____________________________");
	int headingPadding = 4;
	int row, col = 0;
	if(misses >= 30){
		move(headingPadding + board_dimenY*3/2, board_dimenX*6);
		printw("YOU LOSE");
		shouldExit = 1;
		row = headingPadding+ board_dimenY*3;
	} else if(hits >= 100){
		move(headingPadding + board_dimenY*3/2, board_dimenX*6);
		printw("YOU WIN!!!!!!!!!");
		shouldExit = 1;
		row = headingPadding+ board_dimenY*3;
	} else {
		for(i = 0; i < board_dimenX; i++){
			row = headingPadding;
			for(j = 0; j < board_dimenY; j++){
				//get mole data
				int index = j*board_dimenX+i;
				char moleChar = ' ';
				if(moles[index] == 1)
					moleChar = keys[index];
	
				//draw cell
				move(row, col);
				printw("*---*");
				row++;
				move(row, col);
				printw("| %c |", moleChar);
				row++;
				move(row, col);
				printw("*---*");
				row++;
			}
			col = col + 6;
		}
	}

	move(row,0);
	printw("_____________________________");
	row += 2;
	move(row,0);
	printw("Total Hits: %d", hits);
	row++;
	move(row,0);
	printw("Total Misses: %d", misses);
	row = row + 3;
	move(row,0);
	printw("Game Parameters");
	row++;
	move(row,0);
	printw("---------------");
	row++;
	move(row,0);
	printw("Total Number of Moles: \t%d", moleTotal);
	row++;
	move(row,0);
	printw("Max Number of Moles out at a time: \t%d", moleLimit);
	row++;
	move(row,0);
	printw("Hide Time: \tbetween %d ms and %d ms", hideTimeMin, hideTimeMax);
	row++;
	move(row,0);
	printw("Out Time: \tbetween %d ms and %d ms", outTimeMin, outTimeMax);
	row= row + 2;
	move(row,0);
	printw("Press [ESC] to close.");
	

	refresh();

	/**** exit section ****/
	semop(semId, &drawSemBufSignal, 1);
	/**********************/
}

void *keyboardHandler(){
	/* wait for user input to play game */
	draw();
	input = getch();
	while(input != 0x1B && !shouldExit){
		int i;
		int hitIndex = -1;
		for(i = 0; i < board_dimenX*board_dimenY; i++){
			if(keys[i] == input && moles[i] == 1){
				hitIndex = i;
				break;
			}
		}
		if(hitIndex != -1){
			hits++;

			//clear from screen and update semaphore
			moles[hitIndex] = 2;
		} else {
			misses++;
		}
		draw();
		input = getch();
	}

	/* prepare program for clean up */
	printw("\n\nClosing program and cleaning up...");
	refresh();
	shouldExit = 1;

	threadCount--;
	return 0;
}

