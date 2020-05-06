#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <getopt.h>
#include <string.h>
#include <arpa/inet.h>
#include <alloca.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include "../simpledns/hex.h"


int my_verbose = 0;
static struct sockaddr_in my_dst_addr; //, *my_pdst_addr = NULL;
static char* my_targethost = NULL;
static char* my_dumppath = NULL;

time_t my_oldtime;
int my_oldtimenr;

int dowrite(const char*data,  size_t len, char*tag1, char*tag2) {
	char fn[PATH_MAX];
	snprintf(fn,PATH_MAX, "%s/%s-%s", my_dumppath, tag1, tag2);
	FILE*f = fopen(fn, "w");
	if (!f) {
		perror(fn);
		return -1;
	}
	printf("log:%s, len:%ld\n", fn, len);
	size_t w= fwrite(data, len, 1, f);
	if (len != w) {
		printf("len %ld != w: %ld\n", len, w);
	}
	fclose(f);
	return w;
}


int  process(char*data, ssize_t len, struct sockaddr_in* src_addr, socklen_t src_addr_len) {
	printf("process:%ld\n", len);


	return len;
} 


int forward(const char*buf, int len, char*retbuf, int retbuf_sz) {
	int sock = socket(AF_INET, SOCK_STREAM,0);
	int res = -1;
	if (sock < 0) {
		perror("forward:outsock");
		goto forward_end;
	}

	printf("forward:sock: %d target:%s:%d\n",sock, inet_ntoa(my_dst_addr.sin_addr), ntohs(my_dst_addr.sin_port));

	if (connect(sock, (struct sockaddr*)&my_dst_addr, sizeof(my_dst_addr)) < 0) {
		perror("forward:connect");
		goto forward_end;
	}

	int sent =send(sock, buf, len, 0);
	printf("forward:send:%d ,sent:%d\n", len, sent);
	// -1 checken
	bzero(retbuf, retbuf_sz); // gerolf likes to zero
	res = recv(sock, retbuf, retbuf_sz, 0);
	printf("forward:received:%d\n", res);
	forward_end:
	close(sock);
	return res;
}



int log_forward(const char* buf, int len, char* retbuf, int retbuf_maxsz) {

	#define TS_SZ 64
	char ts[TS_SZ];	
	time_t now = time(NULL);
	if (now == my_oldtime) {
		my_oldtimenr++;
	} else {
		my_oldtimenr=0;
		my_oldtime = now;
	}
	snprintf(ts, TS_SZ, "%ld-%d", my_oldtime, my_oldtimenr);


	dowrite(buf, len, "req", ts);	

	int r = forward(buf, len, retbuf, retbuf_maxsz);
	if (r > -1) {
		dowrite(retbuf, r, "res", ts);
	}
	return r;
}




int start(struct in_addr * bind_addr, int port) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		return -1;
	}
	// maybe drop privileges
	struct sockaddr_in me;
	bzero(&me, sizeof(struct sockaddr_in));
	me.sin_family = AF_INET;
	if (bind_addr == NULL) {
		me.sin_addr.s_addr= htonl(INADDR_ANY); // auf alle listenen
	} else {
		memcpy(&me.sin_addr, bind_addr, sizeof(struct in_addr));
	}
	printf("port:%d\n", port);
	me.sin_port = htons(port);
	if (bind(sock, (const struct sockaddr*) &me, sizeof(me)) < 0) {		
		perror("bind");
		return -1;
	}

	#define BUF_SZ 65536
	char*buf =(char*) malloc(BUF_SZ);
	if (!buf) return -1;
	char *retbuf =(char*) malloc(BUF_SZ);
	if (!retbuf) return -1;
	struct sockaddr_in cli_addr;

	if (listen(sock, 50) < 0) {
		perror("listen");
		goto start_end;
	}

	ssize_t len;
	socklen_t cli_len;
	int fd,retlen;
	while(1) {
		cli_len = sizeof(me);
		printf("vor accept\n");
		fd = accept(sock, (struct sockaddr*)&cli_addr, &cli_len);
		if (fd < 0) {
			printf("error in accept: %d\n", fd);
			perror("accept");
			return -1;
			goto start_closesock;
		}	
		len = recv(fd, buf, BUF_SZ, 0);
		if (len < 0) {
			perror("");
			printf("error in recv:%ld\n", len);
			goto start_closesock;
		}
		len = process(buf, len, &cli_addr, cli_len);
		if (len < 0) {
			printf("error in process:%ld\n", len);
			goto start_closesock;
		}
		//print_hex_dump(buf, len);
		len = my_dumppath? log_forward(buf, len, retbuf, BUF_SZ) : forward(buf, len, retbuf, BUF_SZ);


//		len = forward(buf, len, retbuf, BUF_SZ);
		print_hex_dump(retbuf, len);
		if (len < 0) {
			printf("error in forward:%ld\n", len);
			goto start_closesock;
		}
		printf("sending back: %ld\n", len);
		int retlen = send(fd, retbuf, len, 0);
		printf("sent back: %d\n", retlen);
		if (retlen != len) {
			printf("retlen:%d != len:%ld, got confused \n", retlen, len);
		}
		start_closesock:
		close(fd);
	}

//	maybe_drop_privileges();
	start_end:
	if (buf) free(buf); buf = NULL;
	if (retbuf) free(retbuf); retbuf = NULL;
	close(sock);
}



void atshutdown() {
}




int main(int argc, char** argv) {
	atexit(atshutdown);
	struct in_addr* pbind_addr = NULL;
	struct in_addr addr;

	int port = -1;
	int c, l;
	int my_targetport = -1;

	while ((c = getopt(argc,argv,"hb:p:h:s:t:r:vl:")) != -1) {
		switch(c) {
			case 'h' : 
				puts("-b bind-interface -p (in)port -t targethost -r targetport -v verbose -l logprefix");
				return 0;
			break;

			case 'b' :
				if (!optarg || (inet_aton(optarg, &addr) == -1)) {
					printf("invalid bind address:%s\n", optarg);
					exit(123);
				}
				pbind_addr = &addr;
			break;

			case 'p' :
				port = atoi(optarg);
				if (port < 0 || port > 65536) { perror("invalid portnr"); exit(123); }
			break;

			case 't':
				l = strlen(optarg);
				if (!l) {
					perror("-t requires an argument"); exit(123);
				}
				my_targethost=alloca(l+1);
				memcpy(my_targethost, optarg, l+1);
			break;
			case 'r':
				my_targetport = atoi(optarg);
				if (my_targetport < 0 || my_targetport > 65536) { perror("invalid portnr"); exit(123); }
			break;
			case 'v' : my_verbose = 1; break;
			case 'l' :
				l = strlen(optarg);
				my_dumppath = alloca(l+1);
				memcpy(my_dumppath, optarg, l+1);	
		}

	}

	if (my_verbose) {
		printf("bind-addr:\t%s\n", pbind_addr == NULL ? "NULL" : inet_ntoa(*pbind_addr));
		printf("port:\t\t%d\n", port);
		printf("target:\t\t%s\n", my_targethost);
		printf("targetport:\t%d\n", my_targetport);
		printf("dumppath:\t%s\n", my_dumppath);
	}


	if (my_targethost) {
		bzero(&my_dst_addr, sizeof my_dst_addr);
		my_dst_addr.sin_family = AF_INET;
		my_dst_addr.sin_port = htons(my_targetport);
		if (!inet_aton(my_targethost, &my_dst_addr.sin_addr)) {
			errno = EFAULT;
			perror("invalid dest ip");
			exit(123);
		}
	}

	int ret = start(pbind_addr, port);
	return ret;
}
