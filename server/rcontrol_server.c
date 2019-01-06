/* Sample code to access our char device */

#include<stdio.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>

#include<stdio.h>
#include<string.h>    //strlen
#include<stdlib.h>    //strlen
#include<sys/socket.h>
#include<arpa/inet.h> //inet_addr
#include<unistd.h>    //write
#include<pthread.h> //for threading , link with lpthread


#include "rcontrol_common.h"




//the thread function
void *connection_handler(void *);
void *relay_control_task(void *ctx);
int disablePreviousTemporaryTick(relay_t *r, int currTick, int currDay);

typedef struct
{
	int sock;
	int fd;
	relay_t *relayRoot;
} threadParameter_t;



int main(int argc , char *argv[])
{
    int socket_desc , client_sock , c, ppfd;
    struct sockaddr_in server , client;
    relay_t *relayRoot = NULL;
    pthread_t relayControlThread;
    threadParameter_t *control_task_par;
    void* controlTaskRetVal;
    int optval;





	relayAdd(&relayRoot, 1, "skoda");
	relayAdd(&relayRoot, 2, "golf");
	relayAdd(&relayRoot, 3, "unused3");
	//relayAdd(&relayRoot, 4, "unused4");



	ppfd = open("/dev/dev_rcontrol",O_RDWR);
    if (ppfd == -1)
    {
        printf("Could not open /dev/dev_rcontrol");
    }
    else
    {
    	puts("/dev/dev_rcontrol opened");
    }




    //Create socket
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
    if (socket_desc == -1)
    {
        printf("Could not create socket");
    }
    else
    {
    	puts("Socket created");
    }

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons( 8888 );


    optval = 1;
    setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    optval = 1;
    setsockopt(socket_desc, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    puts("calling bind");
    //Bind
    if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
    {
        //print the error message
        perror("bind failed. Error");
        return 1;
    }
    puts("bind done");

    //Listen
    listen(socket_desc , 3);


    puts("starting control task");


    control_task_par = malloc(sizeof(threadParameter_t));

    control_task_par->fd = ppfd;
    control_task_par->relayRoot = relayRoot;
    control_task_par->sock = -1;	/*sock used as flag: 0=stop, !=0 -> running*/


    if(pthread_create( &relayControlThread, NULL, relay_control_task, (void*) control_task_par) < 0)
    {
    	printf("relayControlThread not created -> error\r\n");
    	free(control_task_par);
    	close(ppfd);
    	relayFree(&relayRoot);
    	return -1;
    }

    puts("control task started");



    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);
    while( (client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c)) )
    {
    	threadParameter_t *par;
        pthread_t sniffer_thread;

        puts("Connection accepted");

        par = malloc(sizeof(threadParameter_t));

        par->sock = client_sock;
        par->fd = ppfd;
        par->relayRoot = relayRoot;

        if( pthread_create( &sniffer_thread , NULL ,  connection_handler , (void*) par) < 0)
        {
            perror("could not create thread");

        	relayFree(&relayRoot);
            close(client_sock);
            close(ppfd);

            return 1;
        }

        //Now join the thread , so that we dont terminate before the thread
        //pthread_join( sniffer_thread , NULL);
        puts("Handler assigned");
    }

    if (client_sock < 0)
    {
        perror("accept failed");
        close(ppfd);
        return 1;
    }


    /*stop task*/
    printf("stopping control task...\r\n");
    control_task_par->sock = 0;	/*sock used as flag: 0=stop, !=0 -> running*/

    pthread_join(relayControlThread, (void**)(&controlTaskRetVal));
    printf("stopped!\r\n");

	relayFree(&relayRoot);

    close(ppfd);

    return 0;
}




int serverHandleCommand(threadParameter_t *par, char *msg, int len)
{
	int outLen = 0;
	char *p = msg;
	int relayIdx;
	rcCmdCode_t cmdCode=0;
	relay_t *r;



	p = parseCommandCode(p, ':', &cmdCode);

	//printf("\r\nserverHandleCommand: '%s', ccode=%d\r\n", msg, cmdCode);


	switch(cmdCode)
	{
		case RC_CMD_GET_OUTPUT_COUNT:
			//printf("serverHandleCommand: RC_CMD_GET_OUTPUT_COUNT\r\n");
			snprintf(msg, 16, "%d", numberOfRelays);
			outLen = strlen(msg);
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;
		case RC_CMD_GET_NAME:
			//printf("serverHandleCommand: RC_CMD_GET_NAME\r\n");
			p = parseInt(p, ':', &relayIdx);
			r = relayFind(par->relayRoot, relayIdx);

			//printf("serverHandleCommand: relayIdx = %d\r\n",relayIdx);

			if(r)
			{
				strncpy(msg, r->name, 16);
				//printf("'%s'='%s'\r\n", msg, r->name);
				outLen = strlen(msg);
			}
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;
		case RC_CMD_SET_NAME:
			//printf("serverHandleCommand: RC_CMD_SET_NAME\r\n");
			p = parseInt(p, ':', &relayIdx);
			r = relayFind(par->relayRoot, relayIdx);
			if(r)
			{
				p = parseString(p, ':', r->name, sizeof(r->name));
				outLen = prepareBasicResp(msg, 1);	/*ok*/
			}
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;
		case RC_CMD_GET_DATA:
			//printf("serverHandleCommand: RC_CMD_GET_DATA\r\n");

			p = parseInt(p, ':', &relayIdx);
			r = relayFind(par->relayRoot, relayIdx);

			//printf("serverHandleCommand: relayIdx = %d\r\n",relayIdx);

			if(r)
			{
				int bytes = 0;
				int startOffs = 0;
				int max = (TICKBUF_LEN_WEEK > TICKBUF_MAX_CHUNK ? TICKBUF_MAX_CHUNK : TICKBUF_LEN_WEEK);

				p = parseInt(p, ':', &startOffs);
				p = parseInt(p, ':', &bytes);

				//printf("serverHandleCommand: startOffs = %d, bytes = %d\r\n",startOffs, bytes);


				if((bytes > 0) && (bytes <= max) && (startOffs >= 0) && ((startOffs+bytes) <= TICKBUF_LEN_WEEK))
				{
					memcpy(msg, &(r->tickBuf[startOffs]), bytes);
					outLen = bytes;
					//printf("serverHandleCommand: copied %d bytes\r\n",outLen);
				}
			}
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;
		case RC_CMD_SET_DATA:
			//printf("serverHandleCommand: RC_CMD_SET_DATA\r\n");
			p = parseInt(p, ':', &relayIdx);
			r = relayFind(par->relayRoot, relayIdx);
			if(r)
			{
				int bytes = 0;
				int startOffs = 0;
				int max = (TICKBUF_LEN_WEEK > TICKBUF_MAX_CHUNK ? TICKBUF_MAX_CHUNK : TICKBUF_LEN_WEEK);

				p = parseInt(p, ':', &startOffs);
				p = parseInt(p, ':', &bytes);



				if((bytes > 0) && (bytes <= max) && (startOffs >= 0) && ((startOffs+bytes) <= TICKBUF_LEN_WEEK))
				{
					memcpy(&(r->tickBuf[startOffs]), msg, bytes);
					outLen = bytes;
				}
			}
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;
		case RC_CMD_SET_TICK:
			//printf("serverHandleCommand: RC_CMD_SET_TICK\r\n");
			p = parseInt(p, ':', &relayIdx);
			r = relayFind(par->relayRoot, relayIdx);
			if(r)
			{
				int tick = 0;
				int day = 0;
				char tickVal;
				p = parseInt(p, ':', &tick);
				p = parseInt(p, ':', &day);
				p = parseBytes(p, ':', &tickVal, 1);


				//printf("serverHandleCommand: relayIdx = %d, tick = %d, day = %d, tickVal = 0x%02X\r\n",relayIdx, tick, day, tickVal);


				if((tick >= 0) && (tick < TICKBUF_LEN_DAY)
						&& (day >= 0) && (day <7))
				{
					*TICKBUF_PTR(r, day, tick) = tickVal;
					outLen = prepareBasicResp(msg, 1);	/*ok*/
				}
			}
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;
		case RC_CMD_GET_STATE:
			//printf("serverHandleCommand: RC_CMD_GET_STATE\r\n");
			p = parseInt(p, ':', &relayIdx);
			r = relayFind(par->relayRoot, relayIdx);
			if(r)
			{
				msg[0] = (r->currentState ? '1' : '0');
				msg[1] = 0;
				outLen = 1;
			}
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;
		case RC_CMD_GET_ALL_STATES:
			//printf("serverHandleCommand: RC_CMD_GET_ALL_STATES\r\n");
			r = par->relayRoot;
			outLen=0;
			while(r)
			{
				msg[outLen] = (r->currentState ? '1' : '0');
				outLen++;
				r = (r->next == par->relayRoot ? NULL : r->next);
			}
			msg[outLen] = 0;
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;

		case RC_CMD_GET_RES_MIN:
			//printf("serverHandleCommand: RC_CMD_GET_RES_MIN\r\n");
			snprintf(msg, 16, "%d", RES_MIN);
			outLen = strlen(msg);
			//printf("serverHandleCommand: outLen = %d\r\n",outLen);
			break;


		default:
			printf("serverHandleCommand: Command not supported.\r\n");
			outLen = prepareBasicResp(msg, 0);	/*general fail*/
			break;
	}


	if(outLen==0)
	{
		printf("serverHandleCommand: unknown error\r\n");
		outLen = prepareBasicResp(msg, 0);	/*general fail*/
	}

	return outLen;
}




/*
 * This will handle connection for each client
 * */
void *connection_handler(void *ctx)
{
    //Get the socket descriptor
	threadParameter_t *par = (threadParameter_t*)ctx;
    int read_size;
    char msg[TICKBUF_MSG_MAX_LEN];


    //Receive a message from client
    while((read_size = recv(par->sock, msg, TICKBUF_MSG_MAX_LEN, 0)) > 0)
    {
    	int outLen;

    	/*handle message*/
    	outLen = serverHandleCommand(par, msg, read_size);

    	/*send the message back to client*/
        write(par->sock, msg, outLen);
    }

    if(read_size == 0)
    {
        puts("Client disconnected");
        fflush(stdout);
    }
    else if(read_size == -1)
    {
        perror("recv failed");
    }

    close(par->sock);

    //Free the context struct
    free(ctx);

    return 0;
}
#define NEXT_DAY(d) (((d)+1) % 7)
#define PREV_DAY(d) (((d)-1) >= 0 ? ((d)-1) : 6)

#define NEXT_HOUR(h) (((h)+1) % 24)
#define PREV_HOUR(h) (((h)-1) >= 0 ? ((h)-1) : 23)

#define NEXT_MINUTE(m) (((m)+1) % 60)
#define PREV_MINUTE(m) (((m)-1) >= 0 ? ((m)-1) : 59)


int disablePreviousTemporaryTick(relay_t *r, int currTick, int currDay)
{
	int changed = 0;
	int t, d;


	if(currTick > 0)
	{
		/*normal.. no day changed*/
		t = currTick - 1;
		d = currDay;
	}
	else
	{
		/*last tick of prev day*/
		d = ((currDay > 0) ? currDay-1 : 6);
		t = TICKBUF_LEN_WEEK-1;
	}

	//printf("disablePreviousTemporaryTick: entry #%d, 0x%02X.\r\n", r->relayNo, *TICKBUF_PTR(r, d, t));

	if((*TICKBUF_PTR(r, d, t)) & TICK_CMD_TEMP_MASK)
	{
		printf("disablePreviousTemporaryTick: dropping temporary tick for #%d\r\n", r->relayNo);
		(*TICKBUF_PTR(r, d, t)) &= ~TICK_CMD_TEMP_MASK;		/*clear temporary command*/
		changed = 1;
	}

	return changed;
}


int getAction(struct tm *loctime, int *tick, int *day)
{
	int d, h,m;
	int act;


	d = tm_wday_to_weekday[loctime->tm_wday % 7];


	if(loctime->tm_sec > (60-RCONTROL_KICK_INTERVAL_SEC))
	{
		/*next minute*/
		m = NEXT_MINUTE(loctime->tm_min);
		h = loctime->tm_hour;

		if(m == 0)
		{
			h = NEXT_HOUR(h);

			if(h == 0)
			{
				d = NEXT_DAY(d);
			}
		}
	}
	else
	{
		/*this minute*/
		m = loctime->tm_min;
		h = loctime->tm_hour;
	}

	/*check if it's time to run the tick*/
	act = 1;//(((m % RES_MIN) == 0) ? 1 : 0);

	*tick = convertTimeToTick(h, m);
	*day = d;

	return act;
}


unsigned int usToNextKick(struct tm *loctime)
{
	unsigned int nextEvenSecond = (((loctime->tm_sec /RCONTROL_KICK_INTERVAL_SEC)+1)*RCONTROL_KICK_INTERVAL_SEC);
	unsigned int secondsToWait = nextEvenSecond - loctime->tm_sec;


	return secondsToWait * 1000000UL;
}

void dumpState(threadParameter_t *par)
{
	relay_t *r = par->relayRoot;

	while(r)
	{
		printf("#%d=%s ", r->relayNo, (r->currentState ? "ON" : "OFF"));

		r = (r->next == par->relayRoot ? NULL : r->next);
	}
}



int devTransaction(int fd, relay_t *relay, char *buf, int outBytes, int *inBytes)
{
	int ret = -1;

	//printf("devTransaction: fd=%d, buf = 0x%08X\r\n", fd, buf);


	if((fd>=0) && buf)
	{
		int bytes;
		bytes = write(fd, buf, outBytes);

		//printf("devTransaction: write returned %d\r\n", bytes);

		if(bytes > 0)
		{
			relay->currentState = ((buf[1] != 0) ? 1 : 0);
			//printf("relay(%d).currentState = %d\r\n", relay->relayNo,  relay->currentState);

			/*if inbytes is given, we'll wait for a response.
			Note: The current version of the device does not respond at all*/
			if(inBytes != NULL)
			{
				bytes = read(fd, buf, *inBytes);
				if(bytes >= 0)
				{
					/*something was read*/
					ret = 0;

					*inBytes = bytes;
				}
			}
			else
			{
				ret = 0;
			}
		}
	}

	return ret;
}



void* relay_control_task(void *ctx)
{
	threadParameter_t *par = (threadParameter_t*)ctx;
	int changed = 1;		/*start with updating */

	printf("relay_control_task: entry\r\n");


	while(par->sock)	/*sock used as flag: 0=stop, !=0 -> run */
	{
		time_t curtime;
		struct tm *loctime;
		struct tm loctimeArea;
		unsigned int usToWait;
		int tick;
		int day;

		curtime = time(NULL);
		loctime = localtime_r(&curtime, &loctimeArea);

		if(getAction(loctime, &tick, &day))
		{
			relay_t *r = par->relayRoot;

			//printf("relay_control_task: *3*\r\n");

			while(r)
			{
				int ret;
				char buff[2];
				int newState = 0;
				char tickVal = *TICKBUF_PTR(r, day, tick);

				//printf("relay_control_task: *4*\r\n");

				changed += disablePreviousTemporaryTick(r, tick, day);

				/*check basic value*/
				if((tickVal & TICK_CMD_VAL_MASK) == TICK_CMD_ACT)
				{
					newState = 1;
				}

				/*check temp value*/
				if((tickVal & TICK_CMD_TEMP_MASK) == TICK_CMD_TEMP_ACT)
				{
					newState = 1;
				}
				else if((tickVal & TICK_CMD_TEMP_MASK) == TICK_CMD_TEMP_DEACT)
				{
					newState = 0;
				}

				//printf("relay_control_task: *5*\r\n");


				if(r->currentState != newState)
					changed++;


				// make transaction
				buff[0] = (char)(r->relayNo);
				buff[1] = (char)newState;

				printf("relay_control_task: ACT output #%d = %s -> ", (int)buff[0], (buff[1] ? "ON" : "OFF"));

				ret = devTransaction(par->fd, r, buff, 2, NULL);
				printf("ret = %s\r\n", (ret ? "FAIL" : "OK"));

				/*try next relay*/
				r = (r->next == par->relayRoot ? NULL : r->next);
			}
		}

		if(changed)
		{
			changed = 0;
			printf("relay_control_task: %s:%02d:%02d:%02d currentState= ", weekDays[tm_wday_to_weekday[loctime->tm_wday]], loctime->tm_hour, loctime->tm_min, loctime->tm_sec);
			dumpState(par);
			printf("\r\n");
		}


		/*take a new timestamp*/
		curtime = time(NULL);
		loctime = localtime(&curtime);
		usToWait = usToNextKick(loctime);


//		printf("waiting (%u ms)....\r\n", usToWait/1000);
		usleep(usToWait);
//		printf("waiting done\r\n");

	}

	printf("relay_control_task: quitting...\r\n");

	free(ctx);

	return NULL;
}




#if 0



int client()
{
	int fd=0,ret=0;
	int end =0;
	char buff[80]="";
	
	fd=open("/dev/dev_rcontrol",O_RDWR);
	
	printf("fd :%d\n",fd);
	
	ret=read(fd,buff,10);
	buff[ret]='\0';
	printf("ret = %d\r\n", ret);
	
while(!end)
{
	char ch;


	ch = getchar();
	switch(ch)
	{
		case '1': buff[0] = 1;	buff[1] = 1; break;
		case '2': buff[0] = 2;	buff[1] = 1; break;
		case '3': buff[0] = 3;	buff[1] = 1; break;
		case '4': buff[0] = 4;	buff[1] = 1; break;

		case 'q': buff[0] = 1;	buff[1] = 0; break;
		case 'w': buff[0] = 2;	buff[1] = 0; break;
		case 'e': buff[0] = 3;	buff[1] = 0; break;
		case 'r': buff[0] = 4;	buff[1] = 0; break;

		case 'p': end=1;break;
	}

	ret = write(fd, buff, 2);
	printf("ret = %d\r\n", ret);
}


	printf("buff: %s ;length: %d bytes\n",buff,ret);
	close(fd);
}

#endif

