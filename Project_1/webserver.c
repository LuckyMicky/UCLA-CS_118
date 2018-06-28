/*	A simple web server
	Used the CS118's demo server as skeleton code
	Authors: Aaron Cheng and Guan Zhou
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/wait.h>	// for the waitpid() system call
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <signal.h>	// signal name macros, and the kill() prototype

#define ERRNO_404 "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD><BODY><H1>404 Not Found</H1><P>The requested file was not found on this server.</P></BODY></HTML>"

typedef enum { code_200, code_404 } status_code;
const char delim[1] = " ";
/* Function Prototypes */


char* URL2Char (char *);
void connection_interface(int);
void make_header(int, char *, int, status_code);

void error(char *msg)
{
    perror(msg);
    exit(1);
}


void sigchld_handler(int s)
{
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno, pid;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    struct sigaction sa;          // for signal SIGCHLD

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) & serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    listen(sockfd, 5);

    clilen = sizeof(cli_addr);

    /****** Kill Zombie Processes ******/
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (newsockfd < 0)
            error("ERROR on accept");

        pid = fork(); //create a new process
        if (pid < 0) // fork failed
            error("ERROR on fork");

        if (pid == 0)  { // child process
            close(sockfd);
            connection_interface(newsockfd);
            exit(0);
        }
        else // parent process
            close(newsockfd); // parent doesn't need this 
    } /* end of while */
    return 0; /* we never get here */
}
    
char* URL2Char (char* f_name){
        char buffer[512];
        memset(buffer, 0 , 512);
        int i = 0;
        int pos = 0;
        size_t n = 1;
        while(f_name[i] != '\0'){
            if(f_name[i] != '%'){
                memcpy(buffer+pos, (f_name+i), n);
                i++;
                pos++;
            }
            else{
                memcpy(buffer+pos, " ", n);
                i+=3;
                pos++;
            }
        }
        char* str;
        str = malloc(strlen(buffer) + 1);
        strcpy(str,buffer);
        //printf("%s\n", str);
        return str;
 }       

// Different instance for different connection
void connection_interface(int sock)
{
    int n;
    char buffer[1024];
    memset(buffer, 0, 1024);
    // Dump HTTP Request Message to console
    n = read(sock, buffer, 1023);
    if (n < 0)
        error("ERROR reading from socket");
    buffer[n-1] = '\0';
    printf("HTTP Request Message:\n%s\n", buffer);
    
    char *file_name;
    char* start = strstr(buffer, "GET /");
    if(start == buffer) {
    start += 5;
    }
    // } else {
    // write(sock, "HTTP/1.1 ", sizeof("HTTP/1.1 "));
    // write(sock, "500 Internal Error\n", sizeof("500 Internal Error\n"));
    // error("ERROR request type is not supported");
    // return;
    // }


    char* end = strstr(start, " HTTP/");
    int length = end - start;
    strncpy(file_name, start, length);
    file_name[length] = '\0';

     // printf("%s\n",file_name);
     // printf("%lu\n",strlen(file_name));
     file_name = URL2Char(file_name);

    // Send HTTP Response Message
    if (strlen(file_name) == 0)
    {
        make_header(sock, file_name, 0, code_404);
        send(sock, ERRNO_404, strlen(ERRNO_404), 0);
        return;
    }

    FILE *fp = fopen(file_name, "r");

    //if file not found, return
    if (!fp)
    {
        make_header(sock, file_name, 0, code_404);
        send(sock, ERRNO_404, strlen(ERRNO_404), 0);
        return;
    }

    if (!fseek(fp, 0L, SEEK_END))
    {
        long file_len = ftell(fp);

        //if file length error, return
        if (file_len < 0)
        {
            make_header(sock, file_name, 0, code_404);
            send(sock, ERRNO_404, strlen(ERRNO_404), 0);
            return;
        }

        char *file_buffer = malloc(sizeof(char) * (file_len + 1));
        fseek(fp, 0, SEEK_SET);

        size_t num_bytes = fread(file_buffer, sizeof(char), file_len, fp);
        file_buffer[num_bytes] = '\0';

        make_header(sock, file_name, num_bytes, code_200);

        //send file to socket
        send(sock, file_buffer, num_bytes, 0);

        free(file_buffer);
    }

    fclose(fp);

}


void make_header(int socket, char *file_name, int file_length, status_code sc)
{

    //status section
    char *status;
    if (sc == code_200)
        status = "HTTP/1.1 200 OK\r\n";
    else if (sc == code_404)
        status = "HTTP/1.1 404 Not Found\r\n";
    int position = strlen(status);
    char msg[1024];
    memcpy(msg, status, position);
   
    //header line Connection:
    char *connection = "Connection: close\r\n";
    if(sc == code_200){
        memcpy(msg + position, connection, strlen(connection));
        position += strlen(connection);    
    }
    
    //header line Date:
    time_t raw_time;
    time(&raw_time);
    struct tm *time_info = localtime(&raw_time);
    char current_time[32];
    strftime(current_time, 32, "%a, %d %b %Y %T %Z", time_info);
    char date[64] = "Date: ";
    strcat(date, current_time);
    strcat(date, "\r\n");
    memcpy(msg + position, date, strlen(date));
    position += strlen(date);
     
    //header line Server:
    char *server = "Server: Aaron/1.0\r\n";
    memcpy(msg + position, server, strlen(server));
    position += strlen(server);
    
    //header line Last-Modified:
    struct stat attr;
    stat(file_name, &attr);
    struct tm* lm_info = localtime(&(attr.st_mtime));
    char lm_time[32];
    strftime(lm_time, 32, "%a, %d %b %Y %T %Z", lm_info);
    char last_modified[64] = "Last-Modified: ";
    strcat(last_modified, lm_time);
    strcat(last_modified, "\r\n");
    if(sc == code_200){
        memcpy(msg + position, last_modified, strlen(last_modified));
        position += strlen(last_modified);
    }
    
    //header line Content-Length:
    char content_length[50] = "Content-Length: ";
    char len_buffer[16];
    sprintf(len_buffer, "%d", file_length);
    strcat(content_length, len_buffer);
    strcat(content_length, "\r\n");
    if(sc == code_200){
        memcpy(msg + position, content_length, strlen(content_length));
        position += strlen(content_length);
    }
    
    //header line Content-Type:
    char *dot;
    char *content_type = "Content-Type: text/html\r\n";
    if ((dot = strrchr(file_name, '.')) != NULL)
        if (strcasecmp(dot, ".jpeg") == 0)
            content_type = "Content-Type: image/jpeg\r\n";
    if ((dot = strrchr(file_name, '.')) != NULL)
        if (strcasecmp(dot, ".gif") == 0)
            content_type = "Content-Type: image/gif\r\n";
    memcpy(msg + position, content_type, strlen(content_type));
    position += strlen(content_type);
    memcpy(msg + position, "\r\n\0", 3);
    //send and print completed response message
    send(socket, msg, strlen(msg), 0);
    printf("HTTP Response Message:\n%s\n", msg);

}