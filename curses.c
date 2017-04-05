/* To compile:
 *    gcc this-file.c   -lcurses 
 *     */
#include <curses.h>

int main() {
    initscr();
    noecho();    /* do not echo keyboard input */
    char input;
    move (10, 0);
    printw ("Type a character or ESC");
    input = getch();
    while (input != 0x1B) {  /* Loop until escape */
        move (2, 0); /* line 2, column 0 */
        printw ("You typed %c", input);
	move(2, 5);
	printw ("You typed %c", input);

        //refresh();    // when manual refresh is required
        input = getch();
    }
    endwin();
    return 0;
}
