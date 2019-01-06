#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <asm/current.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include "dev_rcontrol.h"
#include <asm/delay.h>
#include <linux/delay.h>

MODULE_AUTHOR("SamiSaastamoinen");
MODULE_DESCRIPTION("dev_rcontrol");
MODULE_LICENSE("GPL");



/**/

#define PULSE_MS	400


#define BASE_ADDR	0x378







#define CMDQUEUE_LEN	32

typedef struct
{
	int bitNo;
	int activate;
} cmdQueue_t;

static cmdQueue_t cmdQueue[CMDQUEUE_LEN];
static int cmdQueueWIdx=0;
static int cmdQueueRIdx=0;





static int r_init(void);
static void r_cleanup(void);

module_init(r_init);
module_exit(r_cleanup);




int my_open(struct inode *inode,struct file *filep);
int my_release(struct inode *inode,struct file *filep);
ssize_t my_read(struct file *filep,char *buff,size_t count,loff_t *offp );
ssize_t my_write(struct file *filep,const char *buff,size_t count,loff_t *offp );


int addToQueue(int bitNo, int activate);
void handleCmd(int bitNo, int act);
void queueKick(void);
void queueStep(void);
int addToQueue(int bitNo, int activate);
int getFromQueue(int *bitNo, int *activate);

void my_timer_callback( unsigned long data );
void my_nextTimer_callback( unsigned long data );


struct file_operations my_fops={
	open: my_open,
	read: my_read,
	write: my_write,
	release:my_release,
};


static unsigned char activeState = 0;
static struct timer_list my_timer;
static struct timer_list my_nextTimer;
static int g_running=0;





static int r_init(void)
{
	printk("<1>hi\n");

	cmdQueueWIdx = 0;
	cmdQueueRIdx = 0;
	g_running = 0;



	setup_timer( &my_timer, my_timer_callback, 0 );
	setup_timer( &my_nextTimer, my_nextTimer_callback, 0 );

	addToQueue(1,0);
	addToQueue(2,0);
	addToQueue(3,0);
	addToQueue(4,0);


	//queueKick();




	if(register_chrdev(222,"dev_rcontrol",&my_fops))
	{
		printk("<1>failed to register");
	}
	return 0;
}


static void r_cleanup(void)
{
	printk("<1>bye\n");
	unregister_chrdev(222,"my_rcontrol");
	return ;
}








int my_open(struct inode *inode,struct file *filep)
{
	//int ret;
	/*MOD_INC_USE_COUNT;*/ /* increments usage count of module */

	//ret = ioperm(BASE_ADDR, 1, 1);

	printk("driver opened\n");


	return 0;
}

int my_release(struct inode *inode,struct file *filep)
{
	//int ret;

	/*MOD_DEC_USE_COUNT;*/ /* decrements usage count of module */
	//ret = ioperm(BASE_ADDR, 1, 0);

	printk("driver closed\n");

	return 0;
}

#define STATE(tmp, bitNo)  ((tmp >> (bitNo-1)) & 0x01)
#define DUMPSTATE(tmp, bitNo) bitNo, (STATE(tmp, bitNo)?"ON":"OFF")

ssize_t my_read(struct file *filep,char *buff,size_t count,loff_t *offp )
{
	char mybuf[128];
	unsigned char tmp = activeState;



	sprintf(mybuf, "%c%c%c%c", (tmp&0x01) + '0', ((tmp>>1)&0x01) + '0',((tmp>>2)&0x01) + '0', ((tmp>>3)&0x01) + '0');
//	sprintf(mybuf, "%d=%s;%d=%s;%d=%s;%d=%s\n", DUMPSTATE(tmp, 1),DUMPSTATE(tmp, 2),DUMPSTATE(tmp, 3),DUMPSTATE(tmp, 4));


	/* function to copy kernel space buffer to user space*/
	if ( copy_to_user(buff,mybuf,strlen(mybuf)) != 0 )
		printk( "Kernel -> userspace copy failed!\n" );

	return strlen(mybuf);

}





void my_timer_callback( unsigned long data )
{
  printk( "my_timer_callback called (%ld).\n", jiffies );

  outb(0, BASE_ADDR);

  /*pulse cleared, let's wait for next command*/
  mod_timer( &my_nextTimer, jiffies + msecs_to_jiffies(PULSE_MS) );

}


void my_nextTimer_callback( unsigned long data )
{
  printk( "my_nextTimer_callback called (%ld).\n", jiffies );

  queueStep();
}







void handleCmd(int bitNo, int act)
{
	unsigned char out = 0;

	if((bitNo >= 1) && (bitNo <= 4))
	{

		out = 1 << (((bitNo-1)*2) + (act?0:1));

		outb(out, BASE_ADDR);


		if(act)
		{
			activeState |= (1<<(bitNo-1));
		}
		else
		{
			activeState &= ~(1<<(bitNo-1));
		}

		printk("handleCmd: relay=%d, act=%s (0x%02X).\n", bitNo, (act?"ON":"OFF"), out);
	}
}


void queueStep(void)
{
	int bitNo, act;
	int startTimer = 0;
	int stoprunning = 0;
	static DEFINE_SPINLOCK(g_oMtxInterlock);
	unsigned long flags;

	printk("queueStep: started\n");

	g_running = 1;	/**/

	spin_lock_irqsave(&g_oMtxInterlock, flags);
	/* critical section ... */

	if(getFromQueue(&bitNo, &act) == 0)
	{
		/*there is a command waiting*/
		handleCmd(bitNo, act);
		startTimer = 1;
	}
	else
	{
		stoprunning=1;
	}

	spin_unlock_irqrestore(&g_oMtxInterlock, flags);

	if(startTimer)
	{
		int ret;
		ret = mod_timer( &my_timer, jiffies + msecs_to_jiffies(PULSE_MS) );
		if (ret) printk("queueStep: Error in mod_timer\n");
	}



	if(stoprunning)
	{
		printk("queueStep: stoprunning\n");
		g_running = 0;
	}

	printk("queueStep: exit\n");

}


void queueKick(void)
{
	if(!g_running)
	{
		g_running = 1;		/*global flag. Will be cleared after the last time has finished*/

		queueStep();	/*start*/
	}
}



#define IDX_NEXT(idx) (((idx)+1) % CMDQUEUE_LEN)


int addToQueue(int bitNo, int activate)
{
	int ret = -1;

	static DEFINE_SPINLOCK(g_oMtxInterlock);
	unsigned long flags;


	spin_lock_irqsave(&g_oMtxInterlock, flags);
	/* critical section ... */

	/*don't overwrite the read pointer*/
	if(IDX_NEXT(cmdQueueWIdx) != cmdQueueRIdx)
	{
		cmdQueue_t *q = &(cmdQueue[cmdQueueWIdx]);

		q->bitNo = bitNo;
		q->activate = activate;

		cmdQueueWIdx = IDX_NEXT(cmdQueueWIdx);

		ret = 0;	/*OK*/
	}


	spin_unlock_irqrestore(&g_oMtxInterlock, flags);

	if(ret)
		printk("addToQueue: buffer full\n");
	else
		printk("addToQueue: idx = %d: added(bitNo = %d, act = %s)\n", cmdQueueWIdx, bitNo, (activate ? "ON" : "OFF"));


	queueKick();

	return ret;
}

int getFromQueue(int *bitNo, int *activate)
{
	int ret = -1;

	/*don't overwrite the read pointer*/
	if(cmdQueueRIdx != cmdQueueWIdx)
	{
		cmdQueue_t *q = &(cmdQueue[cmdQueueRIdx]);

		*bitNo = q->bitNo;
		*activate = q->activate;

		cmdQueueRIdx = IDX_NEXT(cmdQueueRIdx);

		ret = 0;	/*OK*/
	}

	if(ret)
		printk("getFromQueue: buffer empty\n");

	return ret;
}




ssize_t my_write(struct file *filep,const char *buff,size_t count,loff_t *offp )
{
	ssize_t ret = -1;

	if(count == 2)
	{
		unsigned char cmd[2];

		/* function to copy user space buffer to kernel space*/
		if ( copy_from_user(cmd, buff, count) != 0 )
		{
			printk( "Userspace -> kernel copy failed!\n" );
		}
		else
		{
			int bitNo = (int)cmd[0];
			int act = (int)cmd[1];

			if(addToQueue(bitNo, act) == 0)
			{
				ret = 1;	/*one cmd added*/
			}
		}
	}
	else
	{
		printk("my_write: incorrect length (%d)\n", (int)count);
	}

	return ret;
}



/**/







