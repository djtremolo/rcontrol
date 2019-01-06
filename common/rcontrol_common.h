#ifndef __RCONTROL_COMMON_H__
#define __RCONTROL_COMMON_H__

#include<stdio.h>
#include<unistd.h>
#include <sys/socket.h>    //socket
#include <arpa/inet.h> //inet_addr
#include <time.h>
#include <string.h>    //strlen
#include <ncurses.h>





#define RES_MIN			15			//15 minutes per tick

#define RES_PER_TICK	(60 / RES_MIN)


#define TICK_CMD_VAL_MASK	0x01
#define TICK_CMD_DEACT		0x00
#define TICK_CMD_ACT		0x01

#define TICK_CMD_TEMP_MASK	0x0C

#define TICK_CMD_TEMP_ACT	0x0C
#define TICK_CMD_TEMP_DEACT	0x08





#define TICK_PRINT_STATE_ON			'X'
#define TICK_PRINT_STATE_OFF		'-'
#define TICK_PRINT_STATE_TEMP_ON	'p'
#define TICK_PRINT_STATE_TEMP_OFF	'o'



#define TICKBUF_MAX_CHUNK		1400
#define TICKBUF_HEADER_LEN		32
#define TICKBUF_MSG_MAX_LEN		(TICKBUF_MAX_CHUNK + TICKBUF_HEADER_LEN)



#define TICKBUF_LEN_HOUR	(RES_PER_TICK)
#define TICKBUF_LEN_DAY		(24 * TICKBUF_LEN_HOUR)
#define TICKBUF_LEN_WEEK	(7 * TICKBUF_LEN_DAY)


#define TICK_IDX(day, tick)			(((day)*TICKBUF_LEN_DAY) + (tick))

#define TICKBUF_PTR(rel, day, tick)	(&((rel)->tickBuf[TICK_IDX((day),(tick))]))
#define TICKBUF_PRINT_PTR(rel, day, tick)	(&((rel)->printBuf[TICK_IDX((day),(tick))]))


#define IS_VALID_DAY(day)	(((day) >= 0) && ((day) <= 7))
#define IS_VALID_TICK(tick)	(((tick) >= 0) && ((tick) <= TICKBUF_LEN_DAY))


#define RCONTROL_KICK_INTERVAL_SEC		60




typedef struct relay_s relay_t;


#define RELAY_NAME_MAX	16

struct relay_s
{
	int relayNo;
	unsigned char tickBuf[TICKBUF_LEN_WEEK];
	char printBuf[TICKBUF_LEN_WEEK];

	char name[RELAY_NAME_MAX+1];
	int currentState;

	relay_t *prev;
	relay_t *next;
};


typedef struct
{
	char addr[128];
	int port;
	int sock;
	struct sockaddr_in server;
	char msgBuf[TICKBUF_MSG_MAX_LEN];
} comm_t;




typedef enum
{
	RC_CMD_ILLEGAL=-1,
	RC_CMD_GET_OUTPUT_COUNT = 0,
	RC_CMD_GET_NAME,
	RC_CMD_SET_NAME,
	RC_CMD_GET_DATA,
	RC_CMD_SET_DATA,
	RC_CMD_SET_TICK,
	RC_CMD_GET_STATE,
	RC_CMD_GET_ALL_STATES,
	RC_CMD_GET_RES_MIN,

	RC_RESP_OK,
	RC_RESP_FAIL,

	/**************/
	RC_CMD_MAX_COUNT

} rcCmdCode_t;


typedef struct
{
	rcCmdCode_t cmd;
	char *cmdStr;
} rcCmdDefinition_t;







extern char *weekDays[];
extern relay_t *relayRoot;
extern int tm_wday_to_weekday[];
extern comm_t comm;
extern rcCmdDefinition_t rcCmdDefinitionList[];
extern int numberOfRelays;








void relayInit(relay_t *r);
int relayFetch(comm_t *comm, relay_t *r);
void relayFree(relay_t **root);
relay_t* relayAdd(relay_t **root, int relayNo, char *name);
relay_t* relayFind(relay_t *relayRoot, int relayNo);
void relayToggleTick(relay_t *relay, int *actTick, int *actDay);
void relaySetTick(relay_t *relay, int *actTick, int *actDay, unsigned char newVal);
void relaySetTempPulse(relay_t *relay, int *actTick, int *actDay, int ticks, int set);
void updatePrintSequence(relay_t *relay);

void addTickToTime(int tick, int *actTick, int *actDay);
char* convertTickToTime(int tick, char *buf, int maxLen);
int convertTimeToTick(int hour, int min);
char* convertTickToDayAndTime(int tick, int day, char *buf, int maxLen);



char* parseCommandCode(char *buf, char separator, rcCmdCode_t *cmdCode);
int parseCommand(char *inMsg);
char* parseInt(char *buf, char separator, int *iVal);
char* parseString(char *buf, char separator, char *dst, int maxLen);
char* parseBytes(char *buf, char separator, char *dst, int len);

char* findCmdStrForCommandCode(rcCmdCode_t cmdCode);
rcCmdCode_t findCommandCode(char *cmdStr);
int prepareBasicResp(char *msg, int ok);



/*client to server*/



/*for clients*/
int initializeServerInterface(comm_t *comm, relay_t **relayRoot, char *servAddr, int servPort, int fetch);
void closeServerInterface(comm_t *comm, relay_t **root);


void updateRelayStates();
void updatePrintSequence(relay_t *relay);
int readStateFromServer(comm_t *comm);
int writeTickToServer(comm_t *comm, relay_t *relay, int tick, int day, char val);
int writeNameToServer(comm_t *comm, relay_t *relay);
int readNameFromServer(comm_t *comm, relay_t *relay);
int writeDataToServer(comm_t *comm, relay_t *relay);
int readDataFromServer(comm_t *comm, relay_t *relay);
int readStateFromServer(comm_t *comm);
int readDataFromServer(comm_t *comm, relay_t *relay);
int readRelayCountFromServer(comm_t *comm, int *count);
int readTimeResolutionFromServer(comm_t *comm, int *resmin);
int writeTickToServer(comm_t *comm, relay_t *relay, int tick, int day, char val);

void closeConnection(comm_t *comm);
int openConnection(comm_t *comm);

int serverTransaction(comm_t *comm, int *len);

void getCurrentTimeInTicks(int *tick, int *day);









#endif	//__RCONTROL_COMMON_H__
