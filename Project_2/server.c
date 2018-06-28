#include <stdio.h>
#include <stdbool.h>
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

#define DATAFRAME 1024
#define HEADERSIZE 52
#define DATAFIELD 972
#define WAIT 500

struct packet
{

    int type; // 1 for datagram 2 for ACK and 3 for Retransmission
    int sequence_number;
    int max_number;
    
    int fin; // 0 is in middle of file, 1 is in file
    int error;
    double time;
    char data[DATAFIELD]; 
    int data_size;
    int seq_count;
};


void *transform_addr(struct sockaddr *sa)
{
    return &(((struct sockaddr_in*)sa)->sin_addr);
}

int time_difference(struct timeval end, struct timeval start)
{
    return (((end.tv_sec - start.tv_sec) * 1000000) + (end.tv_usec - start.tv_usec))/1000;
}


void file404(int sockfd, struct sockaddr_storage cli_addr, socklen_t addr_len) {
    struct packet fin_packet;
    fin_packet.fin = 3;
    fin_packet.error = 1;
    int resend = 0;
    while (1) {
        //send fin
        resend++;
        sendto(sockfd, &fin_packet, sizeof(fin_packet), 0, (struct sockaddr *)&cli_addr, addr_len);
        if (resend > 4)
            return;
    }
}

void connect_to_server(struct addrinfo * servinfo, int * sockfd)
{
    struct addrinfo * p;
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((*sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("socket error");
            continue;
        }
        
        if (bind(*sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(*sockfd);
            perror("bind error");
            continue;
        }
        
        break;
    }
    return;
}

void error_on_file(char * msg, int sockfd, struct sockaddr_storage cli_addr, socklen_t addr_len)
{
    fprintf(stderr, "%s\n", msg);
    file404(sockfd, cli_addr, addr_len);
    close(sockfd);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sockfd = -1;
    struct addrinfo serv_addr_info, *servinfo, *p;
    int rcvd;
    long received_bytes;
    struct sockaddr_storage cli_addr;
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];
    int cwnd = 5120;
    struct packet response_packet;
    char filename[DATAFRAME];
    bool final_packet = false;
    int sequence_counter = 0;
    
    if (argc != 2)
    {
        fprintf(stderr,"valid portnumber not provieded: ./server [portnum]\n");
        exit(1);
    }
    
    char *port = argv[1];
    
    memset(&serv_addr_info, 0, sizeof serv_addr_info);
    serv_addr_info.ai_family = AF_INET; //Use IPv4
    serv_addr_info.ai_socktype = SOCK_DGRAM;
    serv_addr_info.ai_flags = AI_PASSIVE; //Use my IP
    
    if ((rcvd = getaddrinfo(NULL, port, &serv_addr_info, &servinfo)) != 0)
    {
        exit(0);
    }
    
    connect_to_server(servinfo, &sockfd);
    
    while(1) {
        
        printf("Waiting for requested filename...\n");
        
        addr_len = sizeof(cli_addr);
        
        if ((received_bytes = recvfrom(sockfd, &filename, sizeof(filename), 0, (struct sockaddr *)&cli_addr, &addr_len)) == -1)
        {
            printf("Error: failed to receive filename\n");
            exit(1);
        }
        
        filename[received_bytes] = '\0';
        printf("Requested filename: \"%s\"\n", filename);
        
        char *source = NULL;
        FILE *fp = fopen(filename, "r");
        int file_length;
        
        if (fp == NULL)
        {
            error_on_file("File not Found\n", sockfd, cli_addr, addr_len);
        }
        
        if (fseek(fp, 0L, SEEK_END) == 0){
            long fsize = ftell(fp);
            
            if (fsize == -1)
            {
                error_on_file("File not Found\n", sockfd, cli_addr, addr_len);
            }
            
            source = malloc(sizeof(char) * (fsize + 1));
            
            if (fseek(fp, 0L, SEEK_SET) != 0)
            {
                error_on_file("File not Found\n", sockfd, cli_addr, addr_len);
            }
            
            file_length = fread(source, sizeof(char), fsize, fp);
            
            if (file_length == 0)
            {
               error_on_file("File not Found\n", sockfd, cli_addr, addr_len);
            }

            source[file_length] = '\0';
        }
        
        fclose(fp);
        

        memset((char *) &response_packet, 0, sizeof(response_packet));
        
        int dynamic_cwnd = cwnd / DATAFRAME;
        int num_packets = (file_length + (DATAFIELD-1)) / DATAFIELD; //number of packets required in file
        response_packet.max_number = num_packets;
        response_packet.sequence_number = 0;
        int start_of_seq = 0;
        int curr_packet = 0;
        int end_of_seq;
        if (dynamic_cwnd <= num_packets)
            end_of_seq = dynamic_cwnd;    
        else
            end_of_seq = num_packets;
            
        struct packet * pack_buff;
        pack_buff = (struct packet *) malloc(dynamic_cwnd * sizeof(struct packet));
        int* ack_table = (int*) malloc(dynamic_cwnd*sizeof(int));
        int i;
        struct timeval start;
        gettimeofday(&start, NULL);
        
        //initialize ack table
        
        for (i = 0; i<dynamic_cwnd; i++)
            ack_table[i] = -2;
        
        //Send SYN packet to client
        while(1) {
            struct packet sync_packet;
            memset((char *) &sync_packet, 0, sizeof(sync_packet));
            sync_packet.type = 1;
            sync_packet.sequence_number = 0;
            sync_packet.fin = 2;
            sendto(sockfd, &sync_packet, sizeof(sync_packet), 0, (struct sockaddr *)&cli_addr, addr_len);
            
            printf("Sending packet %d %d SYN\n", sync_packet.sequence_number, cwnd);
            
            struct packet syn_recv;
            fd_set inSet;
            struct timeval timeout;
            int received;
            int max_retry = 0;
            FD_ZERO(&inSet);
            FD_SET(sockfd, &inSet);
            
            // wait for specified time
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
            
            received = select(sockfd+1, &inSet, NULL, NULL, &timeout);

            if(received < 1)
            {
                if (max_retry > 2) {
                    printf("Reached Maximum Retry Times, stop transmission.\n");
                    max_retry++;
                    break;
                }
                printf("Retransmit SYN packet!\n");
                continue;
            }

            recvfrom(sockfd, &syn_recv, sizeof(syn_recv), 0, (struct sockaddr *) &cli_addr, &addr_len);
            if(syn_recv.sequence_number == 0 && syn_recv.fin == 2) {
                printf("Receiving packet 0\n");
                break;
            }
        }
        
        int offset = 0;
        while(1){
            //check time table and send
            if (ack_table[curr_packet - start_of_seq] == -2 && !final_packet){
                
                int seq_num = (curr_packet*DATAFIELD+1) % 30720;
                offset = curr_packet*DATAFIELD;
                sequence_counter = offset / 30720;
                response_packet.type = 1;
                response_packet.sequence_number = seq_num;
                response_packet.max_number = num_packets;
                response_packet.seq_count = sequence_counter;
                response_packet.fin = 0;
                

                if (curr_packet >= num_packets-1) {
                    final_packet = true;
                }
                

                struct timeval end;
                gettimeofday(&end, NULL);
                int time_diff = time_difference(end, start);
                ack_table[curr_packet - start_of_seq] = time_diff;
                
                //saving in buffer for retransmission
                if (file_length - offset < DATAFIELD) {
                    memcpy(response_packet.data, source+offset, file_length - offset);
                    response_packet.data_size = file_length - offset;
                }
                else {
                    memcpy(response_packet.data, source+offset, DATAFIELD);
                    response_packet.data_size = DATAFIELD;
                }
                memcpy(&(pack_buff[curr_packet - start_of_seq]), &response_packet, sizeof(struct packet));
                pack_buff[curr_packet - start_of_seq].type = 3;
                
                //send
                sendto(sockfd, &response_packet, sizeof(response_packet), 0, (struct sockaddr *)&cli_addr, addr_len);
                
                if (curr_packet < end_of_seq)
                    curr_packet++;
                char* status = "";
                if (response_packet.type == 3)
                    status = "Retransmission";
                else if (response_packet.fin == 1)
                    status = "FIN";
                printf("Sending packet %d %d %s\n", response_packet.sequence_number, cwnd, status);
            }
            
            //resend
            int i = start_of_seq;
            while(i < end_of_seq)
            {
                if (ack_table[i-start_of_seq] > 0){
                    struct timeval end;
                    gettimeofday(&end, NULL);
                    int time_diff = time_difference(end, start);
                    response_packet.time = time_diff - ack_table[i-start_of_seq];
                    if (time_diff - ack_table[i-start_of_seq] >= WAIT){

                        if ((pack_buff[i - start_of_seq]).sequence_number == (pack_buff[i - start_of_seq]).max_number)
                            (pack_buff[i - start_of_seq]).fin = 1;
                        sendto(sockfd, &(pack_buff[i - start_of_seq]), sizeof((pack_buff[i - start_of_seq])), 0, (struct sockaddr *)&cli_addr, addr_len);
                        printf("Sending packet %d %d Retransmission\n", pack_buff[i - start_of_seq].sequence_number, cwnd);
                        ack_table[i-start_of_seq] = time_difference(end, start);
                    }
                }
                i++;

            }
            
            if (ack_table[0] == -1){
                for (i = 0; i<dynamic_cwnd-1; i++){
                    ack_table[i] = ack_table[i+1];
                    memcpy(&(pack_buff[i]), &(pack_buff[i+1]), sizeof(struct packet));
                }
                ack_table[dynamic_cwnd-1] = -2;
                memset(&(pack_buff[dynamic_cwnd-1]), 0, sizeof(struct packet));
                start_of_seq++;
                
                if (end_of_seq<num_packets)
                    end_of_seq++;
                dynamic_cwnd = end_of_seq - start_of_seq;
            }
            
            
            
            fd_set inSet;
            struct timeval timeout;
            int received;
            
            FD_ZERO(&inSet);
            FD_SET(sockfd, &inSet);
            
            // wait for specified time
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
            
            // check for acks
            received = select(sockfd+1, &inSet, NULL, NULL, &timeout);

            if(received < 1)
            {
                //printf("Waiting for ACK timeout.\n");
                continue;
            }
            // otherwise, fetch the ack
            struct packet ack;
            recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *) &cli_addr, &addr_len);
            if(ack.type == 2) {
                int pkt = (ack.sequence_number + ack.seq_count*30720) / DATAFIELD;
                ack_table[pkt-start_of_seq] = -1;
                //printf("Received ACK %d\n", ack.sequence_number);
            }
            
            bool finished = false;
            if (final_packet) {
                int n = 0;
                for(n=0; n < dynamic_cwnd; n++) {
                    if (ack_table[n] > 0) {
                        finished = false;
                        break;
                    }
                    else
                        finished = true;
                }
                if (!finished){
                    /* Resend */
                    for (i = start_of_seq; i<start_of_seq+5; i++){
                        if (ack_table[i-start_of_seq] >= 0){
                            struct timeval end;
                            gettimeofday(&end, NULL);
                            int time_diff = time_difference(end, start);
                            response_packet.time = time_diff - ack_table[i-start_of_seq];
                            if (time_diff - ack_table[i-start_of_seq] >= WAIT){
                                // printf("(%d)\n", time_diff - ack_table[i-start_of_seq]);
                                response_packet.type = 3;
                                if ((pack_buff[i - start_of_seq]).sequence_number
                                    == (pack_buff[i - start_of_seq]).max_number)
                                    (pack_buff[i - start_of_seq]).fin = 1;
                                sendto(sockfd, &(pack_buff[i - start_of_seq]), sizeof((pack_buff[i - start_of_seq])), 0, (struct sockaddr *)&cli_addr, addr_len);
                                printf("Sending packet %d %d Retransmission\n", pack_buff[i - start_of_seq].sequence_number, cwnd);
                                ack_table[i-start_of_seq] = time_difference(end, start);
                            }
                        }
                    }

                }
                else {
                    //send fin and waiting for fin ACK
                    int max_retry = 0;
                    while (1) {
                        struct packet fin_packet;
                        fin_packet.fin = 1;
                        //send fin
                        sendto(sockfd, &fin_packet, sizeof(fin_packet), 0, (struct sockaddr *)&cli_addr, addr_len);
                        printf("Sending packet %d FIN\n", fin_packet.sequence_number);
                        
                        received = select(sockfd+1, &inSet, NULL, NULL, &timeout);
                        
                        if(received < 1)
                        {
                            max_retry++;
                            if (max_retry > 4) {
                                printf("File has been sent, Closing Connection.");
                                close(sockfd);
                                exit(0);
                            }
                            continue;
                        }

                        struct packet fin_ack;
                        recvfrom(sockfd, &fin_ack, sizeof(fin_ack), 0, (struct sockaddr *) &cli_addr, &addr_len);
                        printf("Receiving packet %d FIN_ACK\n", fin_ack.sequence_number);
                        
                        if (fin_ack.fin == 3) 
                        {
                            printf("Receiving packet %d FIN_ACK\n", fin_ack.sequence_number);
                            printf("File has been sent, Closing Connection.");
                            close(sockfd);
                            exit(0);
                        }
                    }
                }
            }
        }
        exit(0);
    }
}