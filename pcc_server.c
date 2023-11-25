/*
pcc_server implementation
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>

//Macros- to improve readability

#define ARR_SIZE 95//Number of readable characters
#define LISTEN_QUEUE_SIZE 10
#define UINT32_T_SIZE 4
#define CHAR_MIN 32
#define CHAR_MAX 126
#define BUF_SIZE (1 << 20)
#define FREE_CONNECTION -1


//Globals- because we need them in more places than just in main...

uint32_t pcc_total[ARR_SIZE] = {0};//The data structure to keep out counters
char sigint_fl = 0;//Flag to notify about sigint
int connection = FREE_CONNECTION;//Will store the return values from accept() and -1 implies no connection is being processed
uint32_t pcc_updates[ARR_SIZE] = {0};//Will store the required info from each BUF_SIZE chunk of data


//Additional functions

/*
Checks if the error that occured is one of the 3 specified in the instructions
*/
int error_check(){
    return ((errno == ETIMEDOUT) || (errno == ECONNRESET) || (errno == EPIPE));
}

/*
This function indicates the end of our program, so we access our array and print the results
*/
void exit_server(){
    for(int i=0; i<ARR_SIZE; i++){
        printf("char '%c' : %u times\n", (i + CHAR_MIN), pcc_total[i]);
    }
    exit(0);
}

/*
Overriding the handler for sigint in order to follow the instructions
*/
void sigint_handler(){
    if(connection  == FREE_CONNECTION){//Not processing a connection now- exit immedietely (and print results)
        exit_server();
    }

    else{//We are processing a connection now. According to the instructions we need to wait until completeion
        sigint_fl= 1;//Now, the next time we iterate over the while(1) loop, we will exit immedietely with exit_server()
    }
}


/*
This function handles updating the pcc data structure and returns the connection's C
*/
int update_pcc(char *buf, int len){
    int C = 0;

    for(int i=0; i<len; i++){
        if(buf[i] >= CHAR_MIN && buf[i] <= CHAR_MAX){//Found a new char
            pcc_updates[(int)(buf[i] - CHAR_MIN)]++;//Adding 1 at the right index in pcc_total
            C++;
        }
    }

    return C;
}

/*
Minimum function
*/
int min(int a, int b){
    return a <= b ? a : b;
}

/*
Changing the SIGINT handling
*/
void change_sigint(){
    struct sigaction sigint;
	sigint.sa_handler = &sigint_handler;//Adding our function
	sigemptyset(&sigint.sa_mask);//Nullifying the signals included in SIGINT
	sigint.sa_flags = SA_RESTART;
	if (sigaction(SIGINT, &sigint, 0) != 0) {
		perror("Cannot change SIGINT handler");
		exit(1);
	}
}

int main(int argc, char *argv[]){
    if(argc != 2){//Invalid number of input arguments
        fprintf(stderr, "Wrong amount of arguments: %s\n", strerror(EINVAL));
        exit(1);
    }

    //Setting the sigaction SIGINT with out new handler
    change_sigint();

    //Creating a socket for the server
    int sock = FREE_CONNECTION;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("Cannot create socket");
        exit(1);
	}

    //As suggested in the instructions, using SO_REUSEADDR to enable quick usage of the port after server terminates
    int rt = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &rt, sizeof(int)) < 0){
        perror("Cannot use setsockopt");
        exit(1);
    }

    //Now we construct the info about the server (similar to recitation code)
    struct sockaddr_in serv_addr;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[1]));//Port is according to our argument
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);//Any address 

    //Now, we bind the socket to the given port (similar to recitation code)
    if(bind(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) != 0){
        perror("Cannot bind to port");
        exit(1);
    }

    //Now, we listen on the port for incoming TCP connections
    if(listen(sock, LISTEN_QUEUE_SIZE) != 0){
        perror("Cannot listen to port");
        exit(1);
    }

    //Allocating the buffer (max size - 1MB (~2^20 bytes))
    char *buf = (char *)malloc(BUF_SIZE);
    if(!buf){
        perror("Cannot allocate memory for buffer");
        exit(1);
    }

    //Now we enter the loop specified in the instructions to handle incoming client connections
    while(1){

        if(sigint_fl){//SIGINT was sent while treating the previous connection, so now after we are done with it we exit
            exit_server();
        }

        //Nullifying pcc_updates from the previous connection's counts
        for(int i=0; i<ARR_SIZE; i++){
            pcc_updates[i] = 0;
        }

        //Accepting a TCP connection
        if((connection = accept(sock, NULL, NULL)) < 0){
            perror("Cannot accept the connection");
            exit(1);
        }

        //Reading from the client

        /*
        Now, we read N, but we need to be more careful than in the client code, since we need
        to identify different error cases and also catch situations when the client dies unexpectedally
        */
    
        uint32_t net_integer;
        int bytes = 1;//1 so the loop will start and not terminate immedietaly

        bytes = recv(connection, &net_integer, sizeof(net_integer), MSG_WAITALL);

        if(bytes < 0){
            if(error_check()){//TCP error- exit this connection and continue to other connections
                close(connection);
                connection = FREE_CONNECTION;
                perror("TCP error while trying to read N from client");
                continue;//Accepting next connection
            }

            else{//A more serious problem- terminate the entire server
                perror("non-TCP error while reading N");
                exit(1);
            }
        }

        else if(bytes != UINT32_T_SIZE){//If we didn't manage to read 4 bytes it means the client died for some reason 
            perror("Connection with the client died unexpectedally while reading N- continue to next client");
            close(connection);
            connection = FREE_CONNECTION;
            continue;
            
        }

        //Now we know that N was read successfully
        uint32_t N = ntohl(net_integer);

        //Reading the data from the client 
        uint32_t C = 0;
        int total_bytes_read = 0;
        int cont_flag = 0;
        int bytes_read_now = 0;
        bytes = 1;

        while(1){
            while(bytes > 0 && bytes_read_now < min(BUF_SIZE, N - total_bytes_read)){
                bytes = recv(connection, buf + bytes_read_now, min(BUF_SIZE - bytes_read_now, N - total_bytes_read), MSG_WAITALL);//CHECK THIS!
                bytes_read_now += bytes;
                total_bytes_read += bytes;
            }

            if(bytes < 0){
                if(error_check()){//TCP error- exit this connection and continue to other connections
                    close(connection);
                    connection = FREE_CONNECTION;
                    perror("TCP error while trying to read N from client");
                    continue;//Accepting next connection
                }

                else{//A more serious problem- terminate the entire server
                    perror("non-TCP error while reading N");
                    exit(1);
                }

            }

            else if(bytes_read_now == BUF_SIZE){//Buffer is full
                C = C + update_pcc(buf, BUF_SIZE);//Updates pcc_total with the buf data
                bytes_read_now = 0;//And we continue to read from the beginning of buf again (overriding old data)
            }
            
            //Here bytes == 0 which means the stream of data from the client has ended

            else if(total_bytes_read == N){//All data has been received
                C = C + update_pcc(buf, bytes_read_now);//Updates pcc_total with the buf data
                break;//Done reading and processing the data from the client, break the inner while(1) loop
            }
           
            else{//Buf isn't full, and we did not reach N, it means the client crashed in the middle!
                perror("Connection with the client died unexpectedally while reading data- continue to next client");
                close(connection);
                connection = FREE_CONNECTION;
                cont_flag = 1;//Flag to continue the outer loop and skip this connection
                break;//Breaking inner while(1) loop
            }

        }
        
        if(cont_flag == 1){//Checking if this is a case when client crashed in the middle
            cont_flag = 0;
            continue;
        }
        
        //Sending the calculated C to the client

        net_integer = ntohl(C);

        bytes = 1;

        bytes = write(connection, &net_integer, sizeof(net_integer));
        if(bytes < 0){
            if(error_check()){//TCP error- exit this connection and continue to other connections
                close(connection);
                connection = FREE_CONNECTION;
                perror("TCP error while trying to write C to client");
                continue;//Accepting next connection
            }

            else{//A more serious problem- terminate the entire server
                perror("non-TCP error while sending C to client");
                exit(1);
            }
        }

        else if(bytes != UINT32_T_SIZE){//If we didn't manage to read 4 bytes it means the client died for some reason      
            perror("Connection with the client died unexpectedally while writing C- continue to next client");
            close(connection);
            connection = FREE_CONNECTION;
            continue;
            
        }

        //We are done with this connection successfully;
        close(connection);

        for(int i=0; i<ARR_SIZE; i++){
            pcc_total[i] += pcc_updates[i];
        }

        connection = FREE_CONNECTION;

        //Now we go back up to the start of the while(1) loop and accept() the next connection on listen
    }

    free(buf);
}
