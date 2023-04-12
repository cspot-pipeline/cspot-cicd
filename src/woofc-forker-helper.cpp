#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <spawn.h>

#include "debug.h"

#define SPLAY (5)


int main(int argc,char **argv, char **env)
{
	int err;
	char hbuff[255];
	int i;
	int j;
	int k;
	char *fargv[2];
	pid_t pid;
	pid_t npid;
	char *menv[12];
	char args[12*255];
	char *str;
	char c;
	int status;
	int splay_count = 0;
#ifdef TRACK
	int hid;
	char tbuff[255];
#endif

	signal(SIGPIPE, SIG_IGN);

#ifdef DEBUG
	fprintf(stdout,"woofc-forker-helper: running\n");
	fflush(stdout);
#endif

	while(1) {
		/*
		 * we need 11 env variables and a handler
		 */
		memset(args,0,sizeof(args));
		err = read(0,args,sizeof(args));
		if(err <= 0) {
			fprintf(stdout,"woofc-forker-helper read error %d\n",err);
			fflush(stdout);
			exit(0);
		}
		j = 0;
		k = 0;
		for(i=0; i < 11; i++) {
			while((args[j] != 0) && (j < sizeof(args))) { // look for NULL terminator
				j++;
			}
			if(j >= sizeof(args)) {
				fprintf(stdout,"woofc-forker-helper corrupt launch %s\n",args);
				fflush(stdout);
				exit(0);
			}
			menv[i] = &args[k];
#ifdef DEBUG
			fprintf(stdout,"woofc-forker-helper: received %s\n",menv[i]);
			fflush(stdout);
#endif
			j++;
			k = j;
		}
		menv[11] = NULL;

		fargv[0] = &args[k]; // handler is last
		fargv[1] = NULL;
#ifdef DEBUG
		fprintf(stdout,"woofc-forker-helper: received handler: %s\n",fargv[0]);
		fflush(stdout);
#endif

#ifdef TRACK
		/*
		 * read the hid
		 */
		err = read(0,tbuff,sizeof(tbuff));
		if(err <= 0) {
			fprintf(stderr,"woofc-forker-helper read %d for handler\n",err);
			exit(0);
		}
		hid = atoi(tbuff);
		/*
		 * read the woof name
		 */
		err = read(0,tbuff,sizeof(tbuff));
		if(err <= 0) {
			fprintf(stderr,"woofc-forker-helper read %d for handler\n",err);
			exit(0);
		}

		printf("%s %d RECVD\n",tbuff,hid);
		fflush(stdout);
#endif
		err = posix_spawn(&pid,fargv[0],NULL,NULL,fargv,menv);
		if(err < 0) {
			printf("woof-forker-helper: spawn of %s failed\n",fargv[0]);
		}
#if 0
		pid = vfork();
//		pid = fork();
		if(pid < 0) {
			fprintf(stdout,"woofc-forker-helper: vfork failed\n");
			fflush(stdout);
			exit(1);
		} else if(pid == 0) {
			/*
		 	 * reset stderr for handler to use
		 	 */
			close(2);
			dup2(1,2);
			/*
			 * close 0 in case of SIGPIPE
			 */
			close(0);
//			execve(hbuff,fargv,menv);
			execve(fargv[0],fargv,menv);
			fprintf(stdout,"execve of %s failed\n",fargv[0]);
			fflush(stdout);
			close(2);
			_exit(0);
		} else {
//printf("Helper[%d]: forked pid %d, %s\n",getpid(),pid,fargv[0]);
//fflush(stdout);
		}
#endif

#ifdef DEBUG
		fprintf(stdout,"woofc-forker-helper: vfork completed for handler %s\n",fargv[0]);
		fflush(stdout);
#endif

		/*
		 * if SPLAY > 0, clean up as best we can when we have over
		 * threshold zombies
		 */
		if(SPLAY != 0) {
			splay_count++;
			if(splay_count >= SPLAY) {
	#ifdef DEBUG
				fprintf(stdout,"woofc-forker-helper: about to wait for %s\n",fargv[0]);
				fflush(stdout);
	#endif
				/*
				 * reap the zombie handlers
				 */
				while((pid = waitpid(-1,&status,WNOHANG)) > 0) {
					splay_count--;
	#ifdef DEBUG
					fprintf(stdout,"woofc-forker-helper: completed wait for %s as proc %d\n",fargv[0],pid);
					fflush(stdout);
	#endif
				}
			}
		}
		/*
		 * send WooFForker completion signal
		 */
		write(2,&c,1);
		/*
		 * if SPLAY is zero, wait
		 */
		if(SPLAY == 0) {
//printf("Helper[%d]: calling wait for pid: %d\n",getpid(),pid);
//fflush(stdout);
			npid = waitpid(pid,&status,0);
			if(npid < 0) {
//printf("Helper[%d]: pid: %d exited with error %d\n",getpid(),npid,errno);
//fflush(stdout);
			}
			if(WIFEXITED(status)) {
//printf("Helper[%d]: pid: %d exited with status %d\n",getpid(),npid,WEXITSTATUS(status));
//fflush(stdout);
			} else if(WIFSIGNALED(status)) {
				if(WTERMSIG(status)) {
//printf("Helper[%d]: pid: %d exited with signal\n",getpid(),npid);
//fflush(stdout);
				} else {
//printf("Helper[%d]: pid: %d exited with signal\n",getpid(),npid);
//fflush(stdout);
				}
			}
		}
			
#ifdef DEBUG
		fprintf(stdout,"woofc-forker-helper: signaled parent for %s after proc %d reaped\n",fargv[0],pid);
		fflush(stdout);
#endif
		fflush(stdout);
	}

	fprintf(stdout,"woofc-forker-helper exiting\n");	
	fflush(stdout);
	exit(0);
}

		
