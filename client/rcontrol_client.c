/*
 C ECHO client example using sockets
 */
#include <stdio.h> //printf
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>    //strlen
#include "rcontrol_common.h"

void dumpState()
{
	relay_t *r = relayRoot;

	while (r)
	{
		printw("[ #%d=%s ]     ", r->relayNo,
				(r->currentState ? "ON " : "OFF"));

		r = (r->next == relayRoot ? NULL : r->next);
	}
}

void drawScreen(relay_t *relay, int width, int height, int *tick_offs,
		int actTick, int actDay)
{
	int dayLen = 3 + 2;
	int margx = 2;
	int startx = dayLen + margx;
	int starty = 6;
	int i;
	int len = width - startx - margx;
	int day;
	int maxLen;
	char tbuf[6 + 1];

	maxLen = (TICKBUF_LEN_DAY - *tick_offs);

	if (len > maxLen)
	{
		len = maxLen;
	}

	if (actTick < (*tick_offs))
	{
		*tick_offs = actTick;
	}
	if (actTick > (((*tick_offs) + len) - 1))
	{
		*tick_offs = (actTick - len) + 1;
	}

	move(2, 2);
	dumpState();

	move(height - 2, 2);
	printw(
			"[q]=Quit, [ARROWS], [Ctrl+ARROWS], [HOME/END]=Navigate, [Tab]=Change output  ");
	move(height - 1, 2);
	printw(
			"[SPACE]=Toggle, [1]=ON, [0]=OFF, [p / Shift+p]=Temporary, [C]=Clear ");

	/*print output name*/
	move(starty, margx);
	printw("Output #%d: '%s': %s %s             ", relay->relayNo, relay->name,
			weekDays[actDay], convertTickToTime(actTick, tbuf, 6));

	starty++;

	move(starty, startx);
	for (i = 0; i < len; i++)
		addch(' ');

	/*print time scale*/
	for (i = 0; i < len; i++)
	{
		int xIdx = *tick_offs + i;

		if ((xIdx % TICKBUF_LEN_HOUR)== 0){
			int maxSLen = len - i;
			char tmp[TICKBUF_LEN_HOUR + 1];

			tmp[0] = '|';
			convertTickToTime(xIdx, &tmp[1], TICKBUF_LEN_HOUR - 1);

			move(starty, startx + i);

			if (strlen(tmp) >= maxSLen)
				tmp[maxSLen] = 0;

			addstr(tmp);
		}
	}
	addstr(" ");
	starty++;

	/*change codes to print characters*/
	updatePrintSequence(relay);

	/*print relay time schedule*/
	for (day = 0; day < 7; day++)
	{
		move(starty + day, margx);
		addstr(weekDays[day]);
		addstr(": ");

		move(starty + day, startx);
		addnstr(TICKBUF_PRINT_PTR(relay, day, *tick_offs), len);
		addstr(" ");
	}

	move(starty + actDay, startx + (actTick - *tick_offs));

}

int myGetCh(void)
{
	int ch = getch();

	if (ch != ERR)
	{
		/*key was pressed!*/
		return ch;
	}
	else
	{
		/*no key pressed*/
		return -1;
	}
}

int main(int argc, char *argv[])
{
	int key, offs = 0;
	int userTick = 0, userDay = 0;
	int sizeupdated = 1;
	relay_t *relay;
	int end = 0;
	int cntr = 0;
	//int ucntr = 0; /*update counter*/
	char *servAddr = (argc > 1 ? argv[1] : "127.0.0.1");
	int servPort = (argc > 2 ? atoi(argv[2]) : 8888);

	memset(&comm, 0, sizeof(comm_t));
	strncpy(comm.addr, servAddr, 127);
	comm.port = servPort;

	if (initializeServerInterface(&comm, &relayRoot, servAddr, servPort, 1) == 0)
	{

		int tCntr = 0;
		relay = relayFind(relayRoot, 1);

		getCurrentTimeInTicks(&userTick, &userDay);

		/*NCURSES*/
		initscr();
		nodelay(stdscr, TRUE);
		keypad(stdscr, TRUE);

		drawScreen(relay, COLS, LINES, &offs, userTick, userDay);

		while (!end)
		{
			key = myGetCh();
			if (key == -1)
			{
				usleep(10 * 1000);

				if (cntr++ > 100)
				{
					cntr = 0;
					relayFetch(&comm, relay);
					updateRelayStates();
					//mvprintw(0,0,"updating %d  ", ucntr++);

					if (++tCntr >= 60)
					{
						tCntr = 0;
						getCurrentTimeInTicks(&userTick, &userDay);
					}
				}

			}
			else
			{
				cntr = 0;
				tCntr = 0;

				switch (key)
				{
					case KEY_RESIZE:
						sizeupdated = 1;
						break;
					case 353:
						relay = relay->prev;
						break;

					case 9:
						relay = relay->next;
						break;

					case 544: /*ctrl+left*/
						if ((userTick % TICKBUF_LEN_HOUR)==0){
							userTick =
									((userTick / TICKBUF_LEN_HOUR)-1)*TICKBUF_LEN_HOUR;
						}
						else
						{
							userTick = ((userTick / TICKBUF_LEN_HOUR))
									* TICKBUF_LEN_HOUR;
						}
						if (userTick < 0)
							userTick = 0;
						break;

					case 559: /*ctrl+right*/
						userTick =
								((userTick / TICKBUF_LEN_HOUR)+1)*TICKBUF_LEN_HOUR;
						if (userTick > (TICKBUF_LEN_DAY - 1))
							userTick = (TICKBUF_LEN_DAY - 1);
						break;

					case 262: /*home*/
						userTick = 0;
						break;
					case 360: /*end*/
						userTick = (TICKBUF_LEN_DAY - 1);
						break;

					case 'p':
						relaySetTempPulse(relay, &userTick, &userDay, 1, 1);
						break;
					case 'P':
						relaySetTempPulse(relay, &userTick, &userDay, 1, 0);
						break;

					case KEY_LEFT:
						if (userTick > 0)
							userTick--;
						//addTickToTime(1, actTick, actDay);
						break;
					case KEY_RIGHT:
						if (userTick < (TICKBUF_LEN_DAY - 1))
							userTick++;
						break;

					case KEY_UP:
						if (userDay > 0)
							userDay--;
						break;
					case KEY_DOWN:
						if (userDay < 6)
							userDay++;
						break;

					case '0':
						relaySetTick(relay, &userTick, &userDay,
								TICK_CMD_DEACT);
						break;

					case '1':
						relaySetTick(relay, &userTick, &userDay,
								TICK_CMD_ACT);
						break;

					case ' ':
						relayToggleTick(relay, &userTick, &userDay);
						break;

#if 0
						case 'C':
						mvprintw(0,0, "Are you sure? Press 'y' to clear! ");
						if(getch() == 'y')
						{
							relayInit(relay);
						}
						clear();
						break;
#endif
					case 'n':
						getCurrentTimeInTicks(&userTick, &userDay);
						break;

					case 'q':
						end = 1;
						break;
				}

			}

			if (sizeupdated)
			{
				clear();
				sizeupdated = 0;
			}

			drawScreen(relay, COLS, LINES, &offs, userTick, userDay);

			refresh();

		}

		relayFree(&relayRoot);

		endwin();

		closeConnection(&comm);
	}


	return 0;
}
