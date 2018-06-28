#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <stdlib.h>

#define DATAFRAME 1024
#define HEADERSIZE 52
#define DATAFIELD 972
#define WAIT 500

struct packet
{   
    int type; // 1 for datagram 2 for ACK and 3 for Retransmission
    int sequence_number;
    int max_number;
    int fin; //0 means in the middle of a file 1 means finished
    int error;
    double time;
    char data[DATAFIELD];
    int data_size;
    int seq_count;
};

void p_msg(char *s){
    fprintf(stderr, "%s\n", s);
    exit(1);
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct addrinfo addr_h, *server_inf, *p;
    int rv;
    int bytesrv;

    if (argc != 4)
        p_msg("the formate should be ./client <srv_hostname> <srv_port_number> <filename>\n");
    
    char *port_num = argv[2];
    memset(&addr_h, 0, sizeof addr_h);
    addr_h.ai_family = AF_INET;
    addr_h.ai_socktype = SOCK_DGRAM;
    
    if ((rv = getaddrinfo(argv[1], port_num, &addr_h, &server_inf)) != 0)
        p_msg((char*) gai_strerror(rv));
    
    // Creating socket by iterating through p.

    for(p = server_inf; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
        {
             fprintf(stderr, "Error Receiving Socket\n");
             continue;
        }
        break;
    }
    
    if (p == NULL)
    {
        fprintf(stderr, "Error binding socket\n");
        return 2;
    }
    
    if ((bytesrv = sendto(sockfd, argv[3], strlen(argv[3]), 0, p->ai_addr, p->ai_addrlen)) < 0)
     p_msg("Error sending filename");
    
    printf("Requested %s as received.data from %s\n", argv[3], argv[1]);
    
    printf("Waiting for the server's file...\n");
    
    char n_Filename[DATAFRAME];
    strcpy(n_Filename, "received.data");
    
    FILE *fp = fopen(n_Filename, "w+");
    
    if (fp==NULL)
        p_msg("Error creating file");
    
    
    int window_size = 5;
    struct packet response_packet;
    memset((char *) &response_packet, 0, sizeof(response_packet));
    
    response_packet.fin = 0;
    response_packet.sequence_number = 0;
    
    struct packet *buffer;
    buffer = (struct packet *) malloc(window_size * sizeof(struct packet));
    int i;
    for (i = 0; i<window_size; i++)
        memset(&(buffer[i]), -1, sizeof(struct packet));
    
    int start_seq = 0;
    int end_seq = window_size;
    char* status = "";
    while(1)
    {
        recvfrom(sockfd, &response_packet, sizeof(response_packet), 0, (struct sockaddr *) p->ai_addr, &(p->ai_addrlen));
        
        if (response_packet.error == 1) {
            printf("Requested file not found!\n");
            return 0;
        }
        // Sending ACK when receving SYN packet
        if (response_packet.sequence_number == 0 && response_packet.fin == 2) {
            printf("Receiving packet 0\n");
            sendto(sockfd, &response_packet, sizeof(response_packet), 0, (struct sockaddr *) p->ai_addr, p->ai_addrlen);
            printf("Sending packet 0 SYN\n");
            continue;
        }
        
        if (response_packet.fin == 1)
            status = "FIN";
        else if (response_packet.fin == 2)
            status = "SYN";
        else if (response_packet.type == 3)
            status = "Retransmission";
        else
            status = "";
        
        fprintf(stdout, "Receiving packet %d %s\n", response_packet.sequence_number, status);
        
           if (end_seq > response_packet.max_number)
            end_seq = response_packet.max_number;
        else
            end_seq = window_size;
        
        int current_pkt = (response_packet.sequence_number + response_packet.seq_count*30720) / DATAFIELD;
         if ((current_pkt - start_seq) >= 0
            && (current_pkt - start_seq) < window_size){
            // send ACK
            struct packet ack_packet;
            memset((char *) &ack_packet, 0, sizeof(ack_packet));
            ack_packet.type = 2;
            ack_packet.sequence_number = response_packet.sequence_number;
            ack_packet.seq_count = response_packet.seq_count;
            sendto(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *) p->ai_addr, p->ai_addrlen);
            printf("Sending packet %d %s\n", ack_packet.sequence_number, status);
            
            // buffer first
            memcpy(&(buffer[current_pkt - start_seq]), &response_packet, sizeof(struct packet));
            
            while (1){
                
                if ((buffer[0].sequence_number+buffer[0].seq_count*30720) / DATAFIELD == start_seq) {
                    fwrite(buffer[0].data, sizeof(char), buffer[0].data_size, fp);
                    
                    for (i = 0; i<window_size-1; i++){ // shift the buffer to the left
                        memcpy(&(buffer[i]), &(buffer[i+1]), sizeof(struct packet));
                    }
                    memset(&(buffer[window_size-1]), -1, sizeof(struct packet));
                    start_seq++;
                }
                else
                    break;
            }
        }
        else if ((current_pkt - start_seq) * (-1) <= window_size
                 && (current_pkt - start_seq) < 0){
            response_packet.type = 2;
            sendto(sockfd, &response_packet, sizeof(response_packet), 0, (struct sockaddr *) p->ai_addr, p->ai_addrlen);
            printf("Sending packet %d Retransmission\n", response_packet.sequence_number);
        }
        
        memset((char *) &response_packet.data, 0, sizeof(response_packet.data));
        if (response_packet.fin == 1) {
            printf("Transmission done.\nClosing connection...\n");
            struct packet fin_packet;
            memset((char *) &fin_packet, 0, sizeof(fin_packet));
            fin_packet.fin = 3;
            fd_set inSet;
            struct timeval timeout;
            
            FD_ZERO(&inSet);
            FD_SET(sockfd, &inSet);
            
            // waiting for specified time
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
            
            int retry = 0;
            while (1) {
                retry++;
                sendto(sockfd, &fin_packet, sizeof(fin_packet), 0, (struct sockaddr *) p->ai_addr, p->ai_addrlen);
                if (retry > 4) {
                    close(sockfd);
                    return 0;
                }
            }
        }
    }
    
    return 0;
}