#include <stdlib.h>
#include "rcontrol_common.h"

#if DEBUG_ENABLED
#define rcPrintf(...) printf(__VA_ARGS__)
#else
#define rcPrintf(...)
#endif



char *weekDays[7] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
relay_t *relayRoot = NULL;
int tm_wday_to_weekday[] = {6,0,1,2,3,4,5};
comm_t comm;
int numberOfRelays=0;


rcCmdDefinition_t rcCmdDefinitionList[RC_CMD_MAX_COUNT] =
{
		{RC_CMD_GET_OUTPUT_COUNT, "GET_OP_COUNT"},
		{RC_CMD_GET_NAME, "GET_NAME"},
		{RC_CMD_SET_NAME, "SET_NAME"},
		{RC_CMD_GET_DATA, "GET_DATA"},
		{RC_CMD_SET_DATA, "SET_DATA"},
		{RC_CMD_SET_TICK, "SET_TICK"},
		{RC_CMD_GET_STATE,"GET_STATE"},
		{RC_CMD_GET_ALL_STATES, "GET_ALL_STATES"},
		{RC_CMD_GET_RES_MIN, "GET_RES_MIN"},
		/**/
		{RC_RESP_OK,"OK"},
		{RC_RESP_FAIL,"ERROR"}
};



int initializeServerInterface(comm_t *comm, relay_t **relayRoot, char *servAddr, int servPort, int fetch)
{
	int ret = -1;
	int resmin = 0;
	int rc = 0;

	if(comm && servAddr && (servPort > 0))
	{
		memset(comm, 0, sizeof(comm_t));
		strncpy(comm->addr, servAddr, 127);
		comm->port = servPort;

		if(openConnection(comm) == 0)
		{
			readTimeResolutionFromServer(comm, &resmin);
			if(resmin != RES_MIN)
			{
				rcPrintf("The time resolution does not match between client (%d) and server (%d). Rebuild is needed.\r\n", RES_MIN, resmin);
				closeConnection(comm);
				return -1;
			}

			readRelayCountFromServer(comm, &rc);
			if(rc > 0)
			{
				int i;

				for(i=0; i<rc; i++)
				{
					relay_t *r;

					r = relayAdd(relayRoot, i+1, "");
					if(r && (readNameFromServer(comm, r)==0))
					{
						rcPrintf("Created output #%d: '%s'\r\n", r->relayNo, r->name);
						ret = 0;	/*at least some relay was OK*/
						if(fetch)
						{
							relayFetch(comm, r);
						}
					}
				}

				updateRelayStates();
			}
		}
	}

	return ret;
}

void closeServerInterface(comm_t *comm, relay_t **root)
{
	relayFree(root);
	closeConnection(comm);
}





void relayInit(relay_t *r)
{
	memset(r->tickBuf, TICK_CMD_DEACT, sizeof(r->tickBuf));
	r->currentState = 0;
	updatePrintSequence(r);
}



void updateRelayStates()
{
	readStateFromServer(&comm);
}


int relayFetch(comm_t *comm, relay_t *r)
{
	int ret = -1;

	ret = readDataFromServer(comm, r);

	return ret;
}


void relayFree(relay_t **root)
{
	relay_t *r = *root, *next;

	while(r)
	{
		next = r->next;

		free(r);

		r = next;

		/*full round done?*/
		if(r == *root)
			break;
	}

	*root = NULL;
}



relay_t* relayAdd(relay_t **root, int relayNo, char *name)
{
	relay_t *r = *root, *prev = NULL, *created = NULL;

	while(r)
	{
		if(r->relayNo == relayNo)
		{
			return r;		/*use duplicate*/
		}

		prev = r;
		r = r->next;

		/*full round done?*/
		if(r == *root)
			break;
	}

	/*not found -> create*/
	created = (relay_t*)malloc(sizeof(relay_t));
	if(created)
	{
		created->next = NULL;
		created->prev = NULL;

		/*init data*/
		relayInit(created);

		created->relayNo = relayNo;

		strncpy(created->name, name, RELAY_NAME_MAX);


		//relayFetch(&comm, created);

		/*add to list*/
		if(*root)
		{
			/*add to tail*/
			if(prev)
			{
				created->next = *root;		/*make it possible to link to next from the last one*/
				created->prev = prev;
				prev->next = created;
				(*root)->prev = created;	/*make it possible to link prev from the root*/
			}
			else
			{
				/*FAIL*/
				//printw("relayAdd: adding failed\r\n");
				free(created);
				created = NULL;
			}
		}
		else
		{
			/*add as root*/
			*root = created;
		}

		numberOfRelays++;
	}

	return created;
}

relay_t* relayFind(relay_t *relayRoot, int relayNo)
{
	relay_t *r = relayRoot;

	while(r)
	{
		if(r->relayNo == relayNo)
		{
			return r;		/*use duplicate*/
		}

		r = r->next;

		/*full round done?*/
		if(r == relayRoot)
			break;
	}


	return NULL;
}

void relaySetTick(relay_t *relay, int *actTick, int *actDay, unsigned char newVal)
{
	if(relay && IS_VALID_DAY(*actDay) && (IS_VALID_TICK(*actTick)))
	{
		unsigned char *ptr = TICKBUF_PTR(relay, *actDay, *actTick);
		*ptr &= ~TICK_CMD_VAL_MASK;
		*ptr |= newVal;

		writeTickToServer(&comm, relay, *actTick, *actDay, *ptr);

	}

	addTickToTime(1, actTick, actDay);
}

void addTickToTime(int tick, int *actTick, int *actDay)
{
	int newTick = *actTick + tick;
	int daysToAdd = newTick / TICKBUF_LEN_DAY;
	int newDay = *actDay + daysToAdd;

	if(newDay < 7)
	{
		*actTick = newTick % TICKBUF_LEN_DAY;
		*actDay = newDay;
	}
	else
	{
		*actTick = TICKBUF_LEN_DAY-1;
		*actDay = 6;
	}
}
#if 0

void addTickToTime(int tick, int *actTick, int *actDay)
{
	int newTick = *actTick - tick;
	int daysToDec = 0;
	int newDay;

	if(newTick < 0)
	{
		daysToDec = ((-1 * newTick) / TICKBUF_LEN_DAY) +1;	/*it will be anyway 1 day, if newTick is more than day, then it might be two or more*/
		newTick += daysToDec * TICKBUF_LEN_DAY;
	}

	newDay = *actDay - daysToDec;


	if(newDay >= 0)
	{
		*actTick = newTick % TICKBUF_LEN_DAY;
		*actDay = newDay;
	}
	else
	{
		*actTick = TICKBUF_LEN_DAY-1;
		*actDay = 6;
	}
}




void stepFwd(int *actTick, int *actDay)
{
	addTickToTime(1, actTick, actDay);
}


#endif

char* convertTickToTime(int tick, char *buf, int maxLen)
{
	int minutes = tick * RES_MIN;
	int hours = minutes / 60;

	minutes = minutes % 60;

	snprintf(buf, maxLen, "%02d:%02d", hours, minutes);

	return buf;
}

char* convertTickToDayAndTime(int tick, int day, char *buf, int maxLen)
{
	int minutes = tick * RES_MIN;
	int hours = minutes / 60;

	minutes = minutes % 60;

	snprintf(buf, maxLen, "%s %02d:%02d", weekDays[tm_wday_to_weekday[day]], hours, minutes);

	return buf;
}


int convertTimeToTick(int hour, int min)
{
	int ticks;

	ticks = (hour * TICKBUF_LEN_HOUR) + (min / RES_MIN);

	return ticks;
}
//////////////////


void getCurrentTimeInTicks(int *tick, int *day)
{
	time_t curtime;
	struct tm *loctime;

	curtime = time(NULL );
	loctime = localtime(&curtime);

	*tick = convertTimeToTick(loctime->tm_hour, loctime->tm_min);
	*day = tm_wday_to_weekday[loctime->tm_wday];

}



void relayToggleTick(relay_t *relay, int *actTick, int *actDay)
{
	if(relay && IS_VALID_DAY(*actDay) && (IS_VALID_TICK(*actTick)))
	{
		char ch = *TICKBUF_PTR(relay, *actDay, *actTick);

		ch ^= TICK_CMD_VAL_MASK;

		*TICKBUF_PTR(relay, *actDay, *actTick) = ch;

		writeTickToServer(&comm, relay, *actTick, *actDay, ch);

	}

	addTickToTime(1, actTick, actDay);

}



void relaySetTempPulse(relay_t *relay, int *actTick, int *actDay, int ticks, int set)
{
	if(relay && IS_VALID_DAY(*actDay) && (IS_VALID_TICK(*actTick)))
	{
		int len = ticks;
		int startIdx = TICK_IDX(*actDay, *actTick);
		unsigned char *ptr = TICKBUF_PTR(relay, *actDay, *actTick);
		int maxLen = TICKBUF_LEN_WEEK - startIdx;
		int i;

		if(len > maxLen)
			len = maxLen;

		for(i=0; i<len; i++)
		{
			/*clear*/
			(*ptr) &= ~TICK_CMD_TEMP_MASK;

			if(set)
				(*ptr) |= TICK_CMD_TEMP_ACT;
			else
				(*ptr) &= ~TICK_CMD_TEMP_ACT;

			writeTickToServer(&comm, relay, *actTick, *actDay, *ptr);


			ptr++;
		}


		addTickToTime(len, actTick, actDay);
	}
}




void updatePrintSequence(relay_t *relay)
{
	int i;

	for(i=0;i<TICKBUF_LEN_WEEK;i++)
	{
		unsigned char *sptr = &(relay->tickBuf[i]);
		char *dptr = &(relay->printBuf[i]);

		if((*sptr & TICK_CMD_TEMP_MASK) == TICK_CMD_TEMP_ACT)
		{
			*dptr = TICK_PRINT_STATE_TEMP_ON;
		}
		else if((*sptr & TICK_CMD_TEMP_MASK) == TICK_CMD_TEMP_DEACT)
		{
			*dptr = TICK_PRINT_STATE_TEMP_OFF;
		}
		else
		{
			switch(*sptr)
			{
				case TICK_CMD_ACT: *dptr = TICK_PRINT_STATE_ON; break;
				case TICK_CMD_DEACT: *dptr = TICK_PRINT_STATE_OFF; break;
				default: *sptr = TICK_CMD_DEACT; *dptr = TICK_PRINT_STATE_OFF; break;
			}
		}
	}
}


char* parseBytes(char *buf, char separator, char *dst, int len)
{
	char *ret = NULL;
	char *p = buf;
	char *d = dst;
	int i;
	int bytesLeft = len;


	if(p)
	{
		for(i=0;i<bytesLeft;i++)
		{
			*(d++) = *(p++);
		}

		ret = &(buf[len]);
	}

	return ret;
}


char* parseString(char *buf, char separator, char *dst, int maxLen)
{
	char *ret = NULL;
	char *p = buf;
	char *d = dst;
	int end = 0;
	int bytesLeft = maxLen;

	if(p)
	{

		while(!end)
		{
			if((*p == 0) || (*p == separator))
			{
				end = 1;
				break;
			}
			else
			{
				if(bytesLeft > 1)
				{
					*(d++) = *p;
					bytesLeft--;
				}
			}

			p++;
		}

		if(*p == separator)
			ret = &(p[1]);
	}

	*d = 0;	/*terminate*/

	return ret;
}

rcCmdCode_t findCommandCode(char *cmdStr)
{
	rcCmdCode_t cmdCode = RC_CMD_ILLEGAL;
	int i;

	for(i=0;i<RC_CMD_MAX_COUNT;i++)
	{
		rcCmdDefinition_t *def = &(rcCmdDefinitionList[i]);

		if(strcmp(cmdStr, def->cmdStr) == 0)
		{
			return def->cmd;
		}
	}

	return cmdCode;
}

char* findCmdStrForCommandCode(rcCmdCode_t cmdCode)
{
	char *ret = NULL;
	int i;

	for(i=0;i<RC_CMD_MAX_COUNT;i++)
	{
		rcCmdDefinition_t *def = &(rcCmdDefinitionList[i]);

		if(def->cmd == cmdCode)
		{
			return def->cmdStr;
		}
	}

	return ret;
}



char* parseCommandCode(char *buf, char separator, rcCmdCode_t *cmdCode)
{
	char cbuf[16];
	char *ret;

	ret = parseString(buf, separator, cbuf, 16);

	*cmdCode = findCommandCode(cbuf);

	return ret;
}


char* parseInt(char *buf, char separator, int *iVal)
{
	char abuf[16];
	char *ret;

	ret = parseString(buf, separator, abuf, 16);

	*iVal = atoi(abuf);

	return ret;
}


int prepareBasicResp(char *msg, int ok)
{
	int len = 0;
	char *cmdStr = findCmdStrForCommandCode((ok ? RC_RESP_OK : RC_RESP_FAIL));

	if(cmdStr)
	{
		strncpy(msg, cmdStr, 16);
		len = strlen(msg);
	}

	return len;
}


int openConnection(comm_t *comm)
{

	rcPrintf("openConnection: Connecting to %s:%d.\r\n", comm->addr, comm->port);

	//Create socket
	comm->sock = socket(AF_INET , SOCK_STREAM , 0);
	if (comm->sock == -1)
	{
		rcPrintf("Could not create socket");
	}

	comm->server.sin_addr.s_addr = inet_addr(comm->addr);
	comm->server.sin_family = AF_INET;
	comm->server.sin_port = htons( 8888 );

	//Connect to remote server
	if (connect(comm->sock , (struct sockaddr *)&(comm->server) , sizeof((comm->server))) < 0)
	{
		perror("connect failed. Error");
		return 1;
	}


	rcPrintf("openConnection: connected successfully.\r\n");

	return 0;
}

void closeConnection(comm_t *comm)
{
	close(comm->sock);
}

int serverTransaction(comm_t *comm, int *len)
{
	int bytes = *len;

	if(bytes > TICKBUF_MSG_MAX_LEN)
		*len = TICKBUF_MSG_MAX_LEN;

	if(send(comm->sock, comm->msgBuf, *len, 0) < 0)
	{
		puts("Send failed");
		return -1;
	}

	//rcPrintf("serverTransaction: TX='%s', len = %d\r\n", comm->msgBuf, *len);



	if((bytes = recv(comm->sock, comm->msgBuf, TICKBUF_MSG_MAX_LEN, 0)) < 0)
	{
		puts("recv failed");
		return -1;
	}

	//rcPrintf("serverTransaction: RX='%s', len = %d\r\n", comm->msgBuf, bytes);

	*len = bytes;
	return 0;
}

int readTimeResolutionFromServer(comm_t *comm, int *resmin)
{
	int ret = -1;
	int len;

	snprintf(comm->msgBuf, TICKBUF_MSG_MAX_LEN, "GET_RES_MIN:");
	len = strlen(comm->msgBuf)+1;

	if(serverTransaction(comm, &len) == 0)
	{
		if(len > 0)
		{
			parseInt(comm->msgBuf, 0, resmin);
			ret = 0;
		}
	}

	return ret;
}



int readRelayCountFromServer(comm_t *comm, int *count)
{
	int ret = -1;
	int len;

	snprintf(comm->msgBuf, TICKBUF_MSG_MAX_LEN, "GET_OP_COUNT:");
	len = strlen(comm->msgBuf)+1;

	if(serverTransaction(comm, &len) == 0)
	{
		if(len > 0)
		{
			parseInt(comm->msgBuf, 0, count);
			ret = 0;
		}
	}

	return ret;
}



int readDataFromServer(comm_t *comm, relay_t *relay)
{
	int ret = -1;
	int len;

	snprintf(comm->msgBuf, TICKBUF_MSG_MAX_LEN, "GET_DATA:%d:%d:%d", relay->relayNo, 0, TICKBUF_LEN_WEEK);
	len = strlen(comm->msgBuf)+1;

	if(serverTransaction(comm, &len) == 0)
	{
		if(len > 0)
		{
			if(len > TICKBUF_LEN_WEEK) len = TICKBUF_LEN_WEEK;

			memcpy(relay->tickBuf, comm->msgBuf, len);
			ret = 0;
		}
	}

	return ret;
}

int writeDataToServer(comm_t *comm, relay_t *relay)
{
	int ret = -1;
	int len;

	snprintf(comm->msgBuf, TICKBUF_MSG_MAX_LEN, "SET_DATA:%d:%d:%d:", relay->relayNo, 0, TICKBUF_LEN_WEEK);
	len = strlen(comm->msgBuf);
	memcpy(&(comm->msgBuf[len]), relay->tickBuf, TICKBUF_LEN_WEEK);
	len += (TICKBUF_LEN_WEEK+1);

	if(serverTransaction(comm, &len) == 0)
	{
		if(len > 0)
		{
			ret = 0;
		}
	}

	return ret;
}


int writeTickToServer(comm_t *comm, relay_t *relay, int tick, int day, char val)
{
	int ret = -1;
	int len;

	snprintf(comm->msgBuf, TICKBUF_MSG_MAX_LEN, "SET_TICK:%d:%d:%d:", relay->relayNo, tick, day);
	len = strlen(comm->msgBuf);
	comm->msgBuf[len++]=val;
	comm->msgBuf[len++]=0;	/*terminate*/

	if(serverTransaction(comm, &len) == 0)
	{
		if(len > 0)
		{
			ret = 0;
		}
	}

	return ret;
}


int readNameFromServer(comm_t *comm, relay_t *relay)
{
	int ret = -1;
	int len;

	//rcPrintf("readNameFromServer entry\r\n");

	snprintf(comm->msgBuf, TICKBUF_MSG_MAX_LEN, "GET_NAME:%d", relay->relayNo);
	len = strlen(comm->msgBuf)+1;

	if(serverTransaction(comm, &len) == 0)
	{
		if(len > 0)
		{
			if(len>RELAY_NAME_MAX) len = RELAY_NAME_MAX;

			strncpy(relay->name, comm->msgBuf, len);

			ret = 0;
		}
	}
	//rcPrintf("readNameFromServer exit\r\n");

	return ret;
}

int readStateFromServer(comm_t *comm)
{
	int ret = -1;
	int len;

	//rcPrintf("readStateFromServer entry\r\n");

	snprintf(comm->msgBuf, TICKBUF_MSG_MAX_LEN, "GET_ALL_STATES:");
	len = strlen(comm->msgBuf)+1;

	if(serverTransaction(comm, &len) == 0)
	{
		if(len == numberOfRelays)
		{
			relay_t *r = relayRoot;
			int i = 0;

			while(r)
			{
				r->currentState = (comm->msgBuf[i++] == '1' ? 1 : 0);

				r = (r->next == relayRoot ? NULL : r->next);
			}

			ret = 0;
		}
	}
	//rcPrintf("readStateFromServer exit (%d)\r\n", ret);

	return ret;
}

int writeNameToServer(comm_t *comm, relay_t *relay)
{
	char msg[1024+1];
	int len;

	snprintf(msg, 127, "SETNAME:%d:%s", relay->relayNo, relay->name);
	len = strlen(msg)+1;

	//Send some data
	if( send(comm->sock , msg , len , 0) < 0)
	{
		puts("Send failed");
		return -1;
	}

	//Receive a reply from the server
	if( recv(comm->sock , msg , 1024, 0) < 0)
	{
		puts("recv failed");
		return -1;
	}

	return 0;
}




