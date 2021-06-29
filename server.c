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

#define MAX_CLIENTS 10
#define BUFLEN 255
#define MAX_NAME_LEN 12
#define MAX_PASS_LEN 16
#define CARD_NUM_LEN 6
#define PIN_LEN 4
#define ATM_MSG "ATM> "
#define UNLOCK_MSG "UNLOCK> "

#define DIE(assertion, call_description)				\
	do {								\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",			\
					__FILE__, __LINE__);		\
			perror(call_description);			\
			exit(EXIT_FAILURE);				\
		}							\
	} while(0)

typedef struct {
	char sname[MAX_NAME_LEN + 1]; // ca la comunisti
	char fname[MAX_NAME_LEN + 1];
	char cardnum[CARD_NUM_LEN + 1];
	char pin[PIN_LEN + 1];
	char secret_pass[MAX_PASS_LEN + 1];
	double credit;
	int lock_status;
	int login_status;
	int fail_login_status;
	int port_logged;
	int wait_for_pass;
} userdata;

void error(char *msg) {
	perror(msg);
	exit(1);
}

void usage(char *file)
{
	fprintf(stderr, "Usage: %s <server_port> <users_data_file>\n", file);
	exit(0);
}

int main(int argc, char *argv[]){

	if (argc != 3)
		usage(argv[0]);

	char buffer[BUFLEN], auxBuffer[BUFLEN], lock_card[CARD_NUM_LEN + 1];
	char get_validinput[11] = "0123456789\0";
	fd_set read_fds;
	fd_set tmp_fds;
	int fd, fdmax, portno, socktcp, sockudp, newsocktcp, newsockudp, clilen, userno;
	int n, i, j, nonexist, get_money, you_may_pass = 0;
	double put_money;
	struct sockaddr_in atm_serv_addr, unlock_serv_addr, cli_addr, unlock_r;
	unsigned int recvudp = sizeof(struct sockaddr);

	//-------------------Sockets-------------------\\
	\\---------------------------------------------//

	socktcp = socket(AF_INET, SOCK_STREAM, 0);
	if (socktcp < 0)
		error("ERROR opening tcp socket");

	portno = atoi(argv[1]);

	memset((char *) &atm_serv_addr, 0, sizeof(atm_serv_addr));
	atm_serv_addr.sin_family = AF_INET;
	atm_serv_addr.sin_addr.s_addr = INADDR_ANY;  // use device ip address
	atm_serv_addr.sin_port = htons(portno);

	if (bind(socktcp, (struct sockaddr *) &atm_serv_addr, sizeof(struct sockaddr)) < 0)
		error("ERROR on tcp binding");

	sockudp = socket(AF_INET, SOCK_DGRAM, 0);
	
	// setup port listener
	unlock_serv_addr.sin_family = AF_INET;
	unlock_serv_addr.sin_addr.s_addr = INADDR_ANY;  // use device ip address
	unlock_serv_addr.sin_port = htons(portno);
	
	if(bind(sockudp, (struct sockaddr*) &unlock_serv_addr, sizeof(struct sockaddr_in)) < 0)
		error("ERROR on udp binding");

	listen(socktcp, MAX_CLIENTS);

	//-------------Read User Data File-------------\\
	\\---------------------------------------------//

	DIE((fd=open(argv[2],O_RDONLY))==-1,"open file");
	FILE *file = fdopen(fd, "r");
	fscanf(file, "%d", &userno);
	userdata data[userno];

	for(i = 0; i < userno; i++) {
		fscanf(file, "%s %s %s %s %s %lf", data[i].sname, data[i].fname, data[i].cardnum, data[i].pin, data[i].secret_pass, &data[i].credit);
		data[i].lock_status = 0;
		data[i].login_status = 0;
		data[i].fail_login_status = 0;
		data[i].port_logged = -1;
		data[i].wait_for_pass = 0;
		data[i].credit += 0.0000001;
	}

	fclose(file);
	close(fd);

	//---------------File descriptors--------------\\
	\\---------------------------------------------//

	// initializes the file descriptor set to have zero bits for all file descriptors
	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);
	// add the listening socket to read_fds set
	FD_SET(socktcp, &read_fds);
	FD_SET(sockudp, &read_fds);
	fdmax = socktcp + sockudp;
	FD_SET(STDIN_FILENO, &read_fds);

	//----------------Communication----------------\\
	\\---------------------------------------------//

	while (1) {
		tmp_fds = read_fds;
		if (select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) == -1)
			error("ERROR in select");

		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &tmp_fds)) {
				if (i == socktcp) {
					// accept new connections
					clilen = sizeof(cli_addr);
					if ((newsocktcp = accept(socktcp, (struct sockaddr *)&cli_addr, &clilen)) == -1) {
						error("ERROR in accept");
					}
					else {
						// add the socket to read_fds set
						FD_SET(newsocktcp, &read_fds);
						if (newsocktcp > fdmax) {
							fdmax = newsocktcp;
						}
					}
				}
				else if (i == sockudp) {

					memset(buffer, 0, BUFLEN);



					if(recvfrom(sockudp, &buffer, BUFLEN, 0, (struct sockaddr*)&unlock_r, &recvudp) < 0)
						error("ERROR in recvfrom");

					for(j = 0; j < userno; j++) {
						if(data[j].wait_for_pass) {
							buffer[strlen(buffer) - 1] = '\0';
							if(!strncmp(buffer, data[j].secret_pass, MAX_PASS_LEN)) {
								memset(auxBuffer, 0, BUFLEN);
								sprintf(auxBuffer, "%sUnlock successful", UNLOCK_MSG);
								sendto(sockudp, &auxBuffer, strlen(auxBuffer), 0, (struct sockaddr*) &unlock_r, sizeof(struct sockaddr));
								data[j].lock_status = 0;
							}
							else {
								memset(auxBuffer, 0, BUFLEN);
								sprintf(auxBuffer, "%s-7: Unlock failed", UNLOCK_MSG);
								sendto(sockudp, &auxBuffer, strlen(auxBuffer), 0, (struct sockaddr*) &unlock_r, sizeof(struct sockaddr));
							}
							data[j].wait_for_pass = 0;
						}
					}

					if(!strncmp(buffer, "unlock", strlen("unlock"))) {
						strncpy(lock_card, buffer + strlen("unlock"), CARD_NUM_LEN);
						if(!strncmp(lock_card, "XXXXXX", CARD_NUM_LEN)) {
							memset(auxBuffer, 0, BUFLEN);
							sprintf(auxBuffer, "%s-10: Could not unlock", UNLOCK_MSG);
							sendto(sockudp, &auxBuffer, strlen(auxBuffer), 0, (struct sockaddr*) &unlock_r, sizeof(struct sockaddr));
							break;
						}
						for(j = 0; j < userno; j++) {
							if(!strncmp(lock_card, data[j].cardnum, CARD_NUM_LEN)) {
								memset(auxBuffer, 0, BUFLEN);
								sprintf(auxBuffer, "%sSend secret password", UNLOCK_MSG);
								sendto(sockudp, &auxBuffer, strlen(auxBuffer), 0, (struct sockaddr*) &unlock_r, sizeof(struct sockaddr));
								data[j].wait_for_pass = 1;
							}
						}
					}
				}
				else if (i == STDIN_FILENO) {
					scanf("%s", buffer);
					if (strcmp(buffer, "quit") == 0) {

						memset(auxBuffer, 0, BUFLEN);
						sprintf(auxBuffer, "%sServerul se inchide", ATM_MSG);

						for(j = 0; j < userno; j++) {
							send(i, auxBuffer, strlen(auxBuffer), 0);
						}

						for(j = 0; j <= fdmax; j++) {
							if (FD_ISSET(j, &tmp_fds));
							if (j != socktcp && j != STDIN_FILENO) {
								send(j, auxBuffer, strlen(auxBuffer), 0);
							}
						}

						close(socktcp);
						close(sockudp);

						return 0;
					}
				}
				else {
					// receive data from client
					memset(buffer, 0, BUFLEN);
					if ((n = recv(i, buffer, sizeof(buffer), 0)) <= 0) {
						if (n == 0) {
						}
						else {
							error("ERROR in recv");
						}
						close(i);
						FD_CLR(i, &read_fds);
					}
					else {
						if(!strncmp(buffer, "login", strlen("login"))) {
							nonexist = 1;
							for(j = 0; j < userno; j++) {
								if(!strncmp(buffer + strlen("login") + 1, data[j].cardnum, CARD_NUM_LEN)) {
									nonexist = 0;
									if(data[j].login_status) {
										memset(auxBuffer, 0, BUFLEN);
										sprintf(auxBuffer, "%s-2: Session already opened", ATM_MSG);
										send(i, auxBuffer, strlen(auxBuffer), 0);
										break;
									}
									if(data[j].lock_status) {
										memset(auxBuffer, 0, BUFLEN);
										sprintf(auxBuffer, "%s-5: Card locked", ATM_MSG);
										send(i, auxBuffer, strlen(auxBuffer), 0);
										break;
									}
									if(!strncmp(buffer + strlen("login") + CARD_NUM_LEN + 2, data[j].pin, PIN_LEN)) {
										memset(auxBuffer, 0, BUFLEN);
										sprintf(auxBuffer, "%sWelcome %s %s", ATM_MSG, data[j].fname, data[j].sname);
										send(i, auxBuffer, strlen(auxBuffer), 0);
										data[j].login_status = 1;
										data[j].fail_login_status = 0;
										data[j].port_logged = i;
										break;
									}
									else {
										data[j].fail_login_status++;
										if(data[j].fail_login_status == 3) {
											memset(auxBuffer, 0, BUFLEN);
											sprintf(auxBuffer, "%s-5: Card locked", ATM_MSG);
											send(i, auxBuffer, strlen(auxBuffer), 0);
											data[j].lock_status = 1;
											break;
										}
										memset(auxBuffer, 0, BUFLEN);
										sprintf(auxBuffer, "%s-3: Incorrect PIN", ATM_MSG);
										send(i, auxBuffer, strlen(auxBuffer), 0);
										nonexist = 0;
										break;
									}
								}
							}
							if(nonexist) {
								memset(auxBuffer, 0, BUFLEN);
								sprintf(auxBuffer, "%s-4: Invalid card number", ATM_MSG);
								send(i, auxBuffer, strlen(auxBuffer), 0);
							}
						}
						else {
							you_may_pass = 0;
							for (j = 0; j < userno; j++) {
								if(data[j].port_logged == i) {
									you_may_pass = 1;
								}
							}
							if(!you_may_pass) {
								memset(auxBuffer, 0, BUFLEN);
								sprintf(auxBuffer, "%s-1: Client authentication required", ATM_MSG);
								send(i, auxBuffer, strlen(auxBuffer), 0);
								break;
							}
						}
						if(!strncmp(buffer, "logout", strlen("logout"))) {
							memset(auxBuffer, 0, BUFLEN);
							sprintf(auxBuffer, "%sDisconnecting from ATM", ATM_MSG);
							send(i, auxBuffer, strlen(auxBuffer), 0);
							for(j = 0; j < userno; j++) {
								if(data[j].port_logged == i) {
									data[j].port_logged = -1;
									data[j].login_status = 0;
									break;
								}
							}
						}
						else if(!strncmp(buffer, "listbalance", strlen("listbalance"))) {
							for(j = 0; j < userno; j++) {
								if(data[j].port_logged == i){
									memset(auxBuffer, 0, BUFLEN);
									sprintf(auxBuffer, "%s%.2lf", ATM_MSG, data[j].credit);
									send(i, auxBuffer, strlen(auxBuffer), 0);
									break;
								}
							}
						}
						else if(!strncmp(buffer, "getmoney", strlen("getmoney"))) {
							memset(auxBuffer, 0, BUFLEN);
							strncpy(auxBuffer, buffer + strlen("getmoney") + 1, strlen(buffer) - strlen("getmoney") - 1);
							auxBuffer[strlen(auxBuffer) - 1] = '\0';
							if(strspn(auxBuffer, get_validinput) != strlen(auxBuffer)) {
								memset(auxBuffer, 0, BUFLEN);
								sprintf(auxBuffer, "%s-9: The amount is not a multiple of 10", ATM_MSG);
								send(i, auxBuffer, strlen(auxBuffer), 0);
							}
							else if(auxBuffer[strlen(auxBuffer) - 1] != '0') {
								memset(auxBuffer, 0, BUFLEN);
								sprintf(auxBuffer, "%s-9: The amount is not a multiple of 10", ATM_MSG);
								send(i, auxBuffer, strlen(auxBuffer), 0);
							}
							else {
								get_money = atoi(auxBuffer);
								for(j = 0; j < userno; j++) {
									if(data[j].port_logged == i){
										if(data[j].credit < get_money) {
											memset(auxBuffer, 0, BUFLEN);
											sprintf(auxBuffer, "%s-8: Insufficient funds", ATM_MSG);
											send(i, auxBuffer, strlen(auxBuffer), 0);
										}
										else {
											data[j].credit = data[j].credit - get_money;
											memset(auxBuffer, 0, BUFLEN);
											sprintf(auxBuffer, "%sYour withdrawal of %d was successful", ATM_MSG, get_money);
											send(i, auxBuffer, strlen(auxBuffer), 0);
										}
										break;
									}
								}
							}
						}
						else if(!strncmp(buffer, "putmoney", strlen("putmoney"))) {
							memset(auxBuffer, 0, BUFLEN);
							strncpy(auxBuffer, buffer + strlen("putmoney") + 1, strlen(buffer) - strlen("putmoney") - 1);
							auxBuffer[strlen(auxBuffer) - 1] = '\0';
							put_money = atof(auxBuffer);
							for(j = 0; j < userno; j++) {
								if(data[j].port_logged == i){
									data[j].credit += put_money;
									memset(auxBuffer, 0, BUFLEN);
									sprintf(auxBuffer, "%sYour deposit of %.2lf was successful", ATM_MSG, put_money);
									send(i, auxBuffer, strlen(auxBuffer), 0);
									break;
								}
							}
						}
						else if(!strncmp(buffer, "quit", strlen("quit"))) {
							for(j = 0; j < userno; j++) {
								if(data[j].port_logged == i){
									data[j].port_logged = -1;
									break;
								}
							}
						}
					}
				}
			}
		}
	}

	return 0; 
}


