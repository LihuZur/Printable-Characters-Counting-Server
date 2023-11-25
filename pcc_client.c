/*
pcc_client implementation
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define BUF_SIZE (1 << 20)
#define UINT32_T_SIZE 4

int main(int argc, char *argv[]){

    if(argc != 4){//Invalid amount of input arguments
        fprintf(stderr, "Wrong amount of arguments: %s\n", strerror(EINVAL));
        exit(1);
    }

    int fd = open(argv[3], O_RDONLY);//Opening the file for reading
    if(fd == -1){
       fprintf(stderr, "Can't open the file: %s\n %s\n", argv[3], strerror(errno));
       exit(1);
    }

    //Now we assume that the arguments (particularly the first 2 arguments) are valid
    //Naming them according to the instructions
    char *ip_address = argv[1];
    int port = atoi(argv[2]);

    //Constrcuting the server address struct (a bit similar to recitation code)
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if(inet_pton(AF_INET, ip_address, &serv_addr.sin_addr) <= 0){
        perror("Error in inet_pton");
        exit(1);
    }

    //Creating the socket (also a bit similar to recitation code)
    int sockfd = -1;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("Could not create client socket");
        exit(1);
	}
    
    //Connecting the client and the server using the socket
	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
		perror("Connection failed!");
        exit(1);
	}

    //Finding N (the number of bytes to be transferred to the server)
    lseek(fd, 0L, SEEK_END);
    uint32_t N = lseek(fd, 0L, SEEK_CUR);
    lseek(fd, 0L, SEEK_SET);

    //Sending N (the size of the file) to the server
    //N is said to be 32-bit unsigned int, so we send 4 bytes (4 * 8 = 32)
    int bytes_written = 0;//We write until this becomes 4
    int bytes = 0;//This variable will save the return value from write each time (similar to recitation code)
    uint32_t net_integer = htonl(N);//Casting to network int
    
    bytes = write(sockfd, &net_integer, sizeof(net_integer));
    if(bytes != sizeof(net_integer)){
        perror("Cannot write N to server");
        exit(1);
    }

    //Allocating the buffer (max size - 1MB (~2^20 bytes))
    char *buf = (char *)malloc(BUF_SIZE);
    if(!buf){
        perror("Cannot allocate memory for buffer");
        exit(1);
    }

    //Now it's time to send our data, but we can send only up to 1MB each time!
    int bytes_to_write = 0;
    while((bytes_to_write = read(fd, buf, BUF_SIZE)) != 0){
        if(bytes_to_write < 0){
            perror("Cannot read from file");
            exit(1);
        }

        bytes_written = 0;
        bytes = 0;
        while(bytes_written < bytes_to_write){
            bytes = write(sockfd, buf + bytes_written, bytes_to_write - bytes_written);
            if(bytes < 0){
                perror("Failed to write data to server");
                exit(1);
            }

            bytes_written += bytes;
        }
    }

    free(buf);//Done sending data to the server

    //Now we need to read C from the server (again 4 bytes, like N)
    bytes = read(sockfd, &net_integer, sizeof(net_integer));
    if(bytes != sizeof(net_integer)){
        perror("Cannot receive C from server");
        exit(1);
    }

    close(sockfd);//Done with the socket so we close the connection

    //Getting C
    uint32_t C = ntohl(net_integer);//net_integer is now set with the bytes of C (using N_or_C_buf)
    
    //Printing the client's results in the requested format
    printf("# of printable characters: %u\n", C);

    return 0;//Done!
}