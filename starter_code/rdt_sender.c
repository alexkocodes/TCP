#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include<math.h>

#include "common.h"
#include "packet.h"

#define STDIN_FD    0
#define RETRY  120 //millisecond

#ifndef max
    #define max(a,b) ((a) > (b) ? (a) : (b))
#endif

int next_seqno=0;
int send_base=0;
// int cwnd = 10; // change here

float cwnd = 1.0; //
int ssthresh = 64; //slow start threshold

int last_sent = 0;
int last_acked = 0;
int ackn_num = 0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;   

tcp_packet *tcp_array[10];

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");

        int i;
        for (i=0; i<cwnd; i++) {
            sndpkt = tcp_array[i];
            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
            VLOG(DEBUG, "Sending packet %d to %s", sndpkt->hdr.seqno , inet_ntoa(serveraddr.sin_addr));
            start_timer();
        };
        
    }
}



void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int main (int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    init_timer(RETRY, resend_packets);
    next_seqno = 0;

    int flag = 1;


    while (1)
    { 
        // while loop for sending
        while(last_sent - last_acked <= ((int)(floor(cwnd))) -1 && flag == 1){

            len = fread(buffer, 1, DATA_SIZE, fp); 
            if ( len <= 0)
            {   
                if(send_base == recvpkt->hdr.ackno - sndpkt->hdr.data_size){ // if we receive the final ack, we send an END flag to tell the receiver we're done
      
                    VLOG(INFO, "End Of File has been reached");
                    sndpkt = make_packet(0);
                    sndpkt->hdr.ctr_flags = END;
                    sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
                            (const struct sockaddr *)&serveraddr, serverlen);
                    tcp_array[((int)(floor(cwnd)))-1] = sndpkt;
                    flag = 0;

                }
                break;
            }
            send_base = next_seqno;
            next_seqno = send_base + len;
            sndpkt = make_packet(len); // create the first packet
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = send_base;
            

            //printf("%d\n", tcp_array[last_sent % 10]->hdr.seqno);
            //do {

                //printf("%s\n", "here2");
                
                VLOG(DEBUG, "Sending packet %d to %s", 
                        send_base, inet_ntoa(serveraddr.sin_addr));

                /*
                * If the sendto is called for the first time, the system will
                * will assign a random port number so that server can send its
                * response to the src port.
                */
                if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0) 
                {
                    error("sendto failed.");
                }
                else{
                    last_sent = send_base/1456;
                    printf("last send: %d and cwnd: %d\n", last_sent, ((int)(floor(cwnd))));
                }

                if(last_sent<cwnd){
                    tcp_array[ last_sent % ((int)(floor(cwnd)))] = sndpkt;
                }
                else{
                    free(tcp_array[0]); // free the acked packet here
                    for(int i=0; i<cwnd-1; i++){
                        tcp_array[i] = tcp_array[i+1];
                    };
                    tcp_array[((int)(floor(cwnd)))-1] = sndpkt;
                }


                start_timer();
                //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                //struct sockaddr *src_addr, socklen_t *addrlen);
            }

                if(flag==0){
                    break;
                }
                
                if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom failed.");
                }
                recvpkt = (tcp_packet *)buffer;
                printf("%d \n", get_data_size(recvpkt));
                
                ackn_num = recvpkt->hdr.ackno/1456 ; // update the last_acked cursor. Move forward by 1.
                if(cwnd <= ssthresh && last_acked < ackn_num){
                    cwnd += ackn_num - last_acked;
                }
                else{
                    cwnd += (1/cwnd);
                }
                
                
                if (ackn_num == last_acked){
                    resend_packets(SIGALRM);
                }
                last_acked = max(last_acked, ackn_num); // only check the biggest ACK. Cumulative ack.
                printf("Returned ack num: %d\n", ackn_num); 

                //printf("%d\n", recvpkt->hdr.ctr_flags);
                if(recvpkt->hdr.ctr_flags == END){
                    break;
                }
                assert(get_data_size(recvpkt) <= DATA_SIZE);
                stop_timer();
    }
    return 0;

}



