/*
 C ECHO client example using sockets
 */
#include <stdio.h> //printf
#include <stdlib.h>
#include <ncurses.h>
#include <string.h>    //strlen
#include "rcontrol_common.h"


char* dumpStr(char *buf, char *data)
{
	int n;
	char *p = buf;

	n = sprintf(p, "%s", data);
	p = &(p[n]);

	return p;
}



char* dumpHTML(char *buf, char *pre, char *data, char *post)
{
	int n;
	char *p = buf;

	n = sprintf(p, "%s%s%s", pre, data, post);
	p = &(p[n]);

	return p;
}







char* dumpActivationInformation(char *buf, relay_t *r)
{
	//int tick, day;
	//unsigned char tickState;
	//int act = 0;
	int n;
	char *p = buf;

	/*
	getCurrentTimeInTicks(&tick, &day);

	tickState = *TICKBUF_PTR(r, day, tick);

	if(((tickState & TICK_CMD_VAL_MASK) == TICK_CMD_ACT) || ((tickState & TICK_CMD_TEMP_MASK) == TICK_CMD_TEMP_ACT))
	{
		act = 1;
	}

*/
	n = sprintf(p, "<td height=\"160\" bgcolor=\"%s\"><a href=\"http://google.com\">%s</a></td><td>%s</td>", (r->currentState ? "green" : "gray"), r->name, "03:30" );
	p = &(p[n]);


	return p;
}



char* dumpRelayState(char *buf, relay_t *r)
{
	char *p = buf;

	p = dumpStr(p, "<tr>");

	p = dumpActivationInformation(p, r);


	p = dumpStr(p, "</tr>");

	return p;
}



char* dumpRelays(relay_t *root, char *buf)
{
	relay_t *r = root;
	char *p = buf;

	p = dumpStr(p, "<table border=\"1\" style=\"width:100%\">");

	while(r)
	{
		p = dumpRelayState(p, r);

		r = (r->next == root ? NULL : r->next);
	}

	p = dumpStr(p, "</table>");

	return p;
}

char* dumpPage(relay_t *root, char *buf)
{
	char *p = buf;

	p = dumpStr(p, "Content-type: text/html\r\n\r\n<script>function changeScreenSize(w,h){window.resizeTo( w,h )}</script><meta http-equiv=\"refresh\" content=\"5\"/><head><STYLE TYPE=\"text/css\"><!--TD{font-family: Arial; font-size: 38pt; text-align: center}---></STYLE></head><body onload=\"changeScreenSize(500,300)\">");


	p = dumpRelays(root, p);

	p = dumpStr(p, "</body>\r\n");


	return p;
}




int main(int argc, char *argv[])
{
//	comm_t comm;
//	relay_t *relay;
	char *servAddr = (argc > 1 ? argv[1] : "127.0.0.1");
	int servPort = (argc > 2 ? atoi(argv[2]) : 8888);
	char httpBuf[2048];
	char *hBuf = httpBuf;




	if(initializeServerInterface(&comm, &relayRoot, servAddr, servPort, 0) == 0)
	{
		hBuf = dumpPage(relayRoot, hBuf);

		printf("%s", httpBuf);

		closeServerInterface(&comm, &relayRoot);
	}


	return 0;
}



