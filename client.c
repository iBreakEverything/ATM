#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFLEN 255
#define CARD_NUM_LEN 6

#define DIE(assertion, call_description)				\
	do {								\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",			\
					__FILE__, __LINE__);		\
			perror(call_description);			\
			exit(EXIT_FAILURE);				\
		}							\
	} while(0)

void error(char *msg) {
	perror(msg);
	exit(1);
}

void usage(char*file)
{
	fprintf(stderr,"Usage: %s <server_ip> <server_port>\n",file);
	exit(0);
}

int main(int argc, char *argv[]){

	if (argc != 3)
		usage(argv[0]);

	char buffer[BUFLEN], auxBuffer[BUFLEN], filename[BUFLEN], last_locked_card[CARD_NUM_LEN + 1];
	int i, fd, fdmax, portno, socktcp, sockudp, recvret, logged = 0, secret = 0;
	struct sockaddr_in atm_serv_addr, unlock_serv_addr, unlock_r;
	fd_set read_fds, tmp_fds;
	unsigned int recvudp = sizeof(struct sockaddr);

	//-------------------Sockets-------------------\\
	\\---------------------------------------------//

	socktcp = socket(AF_INET, SOCK_STREAM, 0);
	if (socktcp < 0)
		error("ERROR opening tcp socket");

	portno = atoi(argv[2]);

	memset((char *) &atm_serv_addr, 0, sizeof(atm_serv_addr));
	atm_serv_addr.sin_family = AF_INET;
	atm_serv_addr.sin_addr.s_addr = INADDR_ANY;
	atm_serv_addr.sin_port = htons(portno);

	sockudp = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockudp < 0)
		error("ERROR opening udp socket");
	
	// setup port listener
	unlock_serv_addr.sin_family = AF_INET;
	inet_aton(argv[1], &unlock_serv_addr.sin_addr);
	unlock_serv_addr.sin_port = htons(portno);

	//----------------Open Log File----------------\\
	\\---------------------------------------------//

	sprintf(filename, "client-%d.log", getpid());
	DIE((fd=open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644))==-1, "open file");

	//---------------File descriptors--------------\\
	\\---------------------------------------------//

	if (connect(socktcp,(struct sockaddr*) &atm_serv_addr,sizeof(atm_serv_addr)) < 0)
		error("ERROR connecting");

	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

	FD_SET(socktcp, &read_fds);
	FD_SET(sockudp, &read_fds);
	FD_SET(STDIN_FILENO, &read_fds);

	fdmax = socktcp < sockudp ? sockudp : socktcp;
	strncpy(last_locked_card, "XXXXXX\0", CARD_NUM_LEN + 1);

	//----------------Command Calls----------------\\
	\\---------------------------------------------//

	while(1) {

		tmp_fds = read_fds;
		if (select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) == -1)
			error("ERROR in select");

		for(i = 0; i <= fdmax; i++) {

			if (FD_ISSET(i, &tmp_fds)) {

				if (i == STDIN_FILENO) {

					memset(buffer, 0 , BUFLEN);
					fgets(buffer, BUFLEN - 1, stdin);
					write(fd, &buffer, strlen(buffer));

					if(secret) {
						sendto(sockudp, &buffer, strlen(buffer), 0, (struct sockaddr*) &unlock_serv_addr, sizeof(struct sockaddr));
						break;
					}

					if((!strncmp(buffer, "login", strlen("login"))) || (!strncmp(buffer, "logout", strlen("logout"))) || (!strncmp(buffer, "list", strlen("list"))) || (!strncmp(buffer, "getmoney", strlen("getmoney"))) || (!strncmp(buffer, "putmoney", strlen("putmoney")))) {
						
						if(!strncmp(buffer, "login", strlen("login"))) {
							strncpy(last_locked_card, buffer + strlen("login") + 1, CARD_NUM_LEN);
						}

						if(logged && !strncmp(buffer, "login", strlen("login"))) {
							sprintf(auxBuffer, "-2: Session already opened\n");
							printf("%s", auxBuffer);
							write(fd, &auxBuffer, strlen(auxBuffer));
							break;
						}

						if(!logged && strstr(buffer, "logout")) {
							sprintf(auxBuffer, "-1: Client authentication required\n");
							printf("%s", auxBuffer);
							write(fd, &auxBuffer, strlen(auxBuffer));
							break;
						}

						if (send(socktcp, buffer, strlen(buffer), 0) < 0)
							error("ERROR writing to tcp socket");
					}
					else if(!strncmp(buffer, "unlock", strlen("unlock"))) {
						buffer[strlen("unlock")] = '\0';
						strncat(buffer, last_locked_card, CARD_NUM_LEN);
						sendto(sockudp, &buffer, strlen("unlockXXXXXX"), 0, (struct sockaddr*) &unlock_serv_addr, sizeof(struct sockaddr));
					}
					else if(!strncmp(buffer, "quit", strlen("quit"))) {

						if (send(socktcp, buffer, strlen(buffer), 0) < 0)
							error("ERROR writing to tcp socket");

						close(socktcp);
						close(sockudp);

						close(fd);

						return 0;
					}
					else {
						printf("-404: Command not found\n");
					}
				}
				else if(i == sockudp){

					// receive data from server
					memset(buffer, 0, BUFLEN);

					if(recvfrom(sockudp, &buffer, BUFLEN, 0, (struct sockaddr*)&unlock_serv_addr, &recvudp) < 0)
						error("ERROR in recvfrom");

					printf("%s\n", buffer);

					if(strstr(buffer, "Secret password") != NULL)
						secret = 1;

					if(strstr(buffer, "Client unlocked") || strstr(buffer, "Failed to unlock"))
						secret = 0;

					write(fd, &buffer, recvret);
					write(fd, "\n", 2);
				}
				else if(i == socktcp){
					// receive data from server
					memset(buffer, 0, BUFLEN);
					if ((recvret = recv(i, buffer, sizeof(buffer), 0)) <= 0) {
						if (recvret == 0) {

							close(socktcp);
							close(sockudp);
							close(fd);

							return 0;
						}
						else {
							error("ERROR in recv");
						}
					}
					else {
						printf("%s\n", buffer);

						if(strstr(buffer, "Welcome") && !logged)
							logged = 1;

						if(strstr(buffer, "Disconnecting") && logged)
							logged = 0;

						write(fd, &buffer, recvret);
						write(fd, "\n", 2);
					}
				}
			}
		}
				
				
	}

	return 0;
}