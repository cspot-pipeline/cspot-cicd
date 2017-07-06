#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include "woofc.h"

static char *WooF_dir;

int WooFInit()
{
	struct timeval tm;
	int err;

	gettimeofday(&tm,NULL);
	srand48(tm.tv_sec+tm.tv_usec);

	WooF_dir = getenv("WOOFC_DIR");
	if(WooF_dir == NULL) {
		WooF_dir = DEFAULT_WOOF_DIR;
	}

	if(strcmp(WooF_dir,"/") == 0) {
		fprintf(stderr,"WooFInit: WOOFC_DIR can't be %s\n",
				WooF_dir);
		exit(1);
	}

	err = mkdir(WooF_dir,0600);
	if((err < 0) && (errno == EEXIST)) {
		return(1);
	}

	if(err >= 0) {
		return(1);
	}

	perror("WooFInit");
	return(-1);
}

WOOF *WooFCreate(char *name,
	       unsigned long element_size,
	       unsigned long history_size)
{
	WOOF *wf;
	MIO *mio;
	unsigned long space;
	char local_name[4096];
	char fname[20];

	/*
	 * each element gets a seq_no and log index so we can handle
	 * function cancel if we wrap
	 */
	space = ((history_size+1) * (element_size +sizeof(ELID))) + 
			sizeof(WOOF);

	if(WooF_dir == NULL) {
		fprintf(stderr,"WooFCreate: must init system\n");
		fflush(stderr);
		exit(1);
	}

	memset(local_name,0,sizeof(local_name));
	strncpy(local_name,WooF_dir,sizeof(local_name));
	if(local_name[strlen(local_name)-1] != '/') {
		strncat(local_name,"/",1);
	}


	if(name != NULL) {
		strncat(local_name,name,sizeof(local_name));
	} else {
		sprintf(fname,"woof-%10.0",drand48()*399999999);
		strncat(local_name,fname,sizeof(fname));
	}
	mio = MIOOpen(local_name,"w+",space);
	if(mio == NULL) {
		return(NULL);
	}

	wf = (WOOF *)MIOAddr(mio);
	memset(wf,0,sizeof(WOOF));
	wf->mio = mio;

	if(name != NULL) {
		strncpy(wf->filename,name,sizeof(wf->filename));
	} else {
		strncpy(wf->filename,fname,sizeof(wf->filename));
	}

	memset(wf->handler_name,0,sizeof(handler_name));
	strncpy(wf->handler_name,name,sizeof(wf->handler_name));
	strncat(wf->handler_name,".handler",sizeof(wf->handler_name)-strlen(wf->handler_name));
	wf->history_size = history_size;
	wf->element_size = element_size;
	wf->seq_no = 1;

	InitSem(&wf->mutex,1);
	InitSem(&wf->tail_wait,0);

	return(wf);
}

void WooFFree(WOOF *wf)
{
	MIOClose(wf->mio);

	return;
}

		
int WooFPut(WOOF *wf, void *element)
{
	pthread_t tid;
	WOOFARG *wa;
	unsigned long next;
	unsigned char *buf;
	unsigned char *ptr;
	ELID *el_id;
	unsigned long seq_no;
	unsigned long ndx;
	int err;
	char woof_shepherd_dir[2048];
	char launch_string[4096];

	P(&wf->mutex);
	/*
	 * find the next spot
	 */
	next = (wf->head + 1) % wf->history_size;

	ndx = next;


	/*
	 * write the data and record the indices
	 */
	buf = (unsigned char *)(MIOAddr(wf->mio)+sizeof(WOOF));
	ptr = buf + (next * (wf->element_size + sizeof(ELID)));
	el_id = (ELID *)(ptr+wf->element_size);

	/*
	 * tail is the last valid place where data could go
	 * check to see if it is allocated to a function that
	 * has yet to complete
	 */
	while((el_id->busy == 1) && (next == wf->tail)) {
		V(&wf->mutex);
		P(&wf->tail_wait);
		P(&wf->mutex);
		next = (wf->head + 1) % wf->history_size;
		ptr = buf + (next * (wf->element_size + sizeof(ELID)));
		el_id = (ELID *)(ptr+wf->element_size);
	}

	/*
	 * write the element
	 */
	memcpy(ptr,element,wf->element_size);
	/*
	 * and elemant meta data after it
	 */
	el_id->seq_no = wf->seq_no;
	el_id->busy = 1;

	/*
	 * update circular buffer
	 */
	ndx = wf->head = next;
	if(next == wf->tail) {
		wf->tail = (wf->tail + 1) % wf->history_size;
	}
	seq_no = wf->seq_no;
	wf->seq_no++;
	V(&wf->mutex);

	 

	/*
	 * now launch the handler
	 */

	/*
	 * find the last directory in the path
	 */
	pathp = strrchr(Woof_dir,"/");
	if(pathp == NULL) {
		fprintf(stderr,"couldn't find leaf dir in %s\n",
			WooF_dir);
		exit(1);
	}

	strncpy(woof_shepherd_dir,pathp,sizeof(woof_shepherd_dir));

	memset(launch_string,0,sizeof(launch_string));

	sprintf(launch_string, "docker run -it\
		 -e WOOF_SHEPHERD_DIR=%s\
		 -e WOOF_SHEPHERD_NAME=%s\
		 -e WOOF_SHEPHERD_SIZE=%lu\
		 -e WOOF_SHEPHERD_NDX=%lu\
		 -e WOOF_SHEPHERD_SEQNO=%lu\
		 -v %s:%s\
		 centos:7\
		 %s",
			woof_shepherd_dir,
			wf->filename,
			wf->mio->size,
			ndx,
			seq_no,
			WooF_dir,pathp,
			wf->handler_name);
			

XXX launch it here

	err = pthread_create(&tid,NULL,WooFThread,(void *)wa);
	if(err < 0) {
		free(wa);
		/* LOGGING
		 * log thread create failure here
		 */
		return(-1);
	}
	pthread_detach(tid);

	return(1);
}

	


	
