#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>

#define MAX_BUFFER 256

int clientNo = 0;
int deliverPide = 0;
pthread_mutex_t clientNoMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t deliveredPideMutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int clientId;
    char hostname[256];
    int port;
    int x;
    int y;
    int clientCount;
    int townx;
    int towny;
} client_info_t;

void* handle_client(void* arg) {
    client_info_t* info = (client_info_t*)arg;
    int sockfd;
    struct sockaddr_in serverAddr;
    char buffer[MAX_BUFFER];

    
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        pthread_exit(NULL);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(info->port);

    
    if (inet_pton(AF_INET, info->hostname, &serverAddr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/ Address not supported\n");
        close(sockfd);
        pthread_exit(NULL);
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        pthread_exit(NULL);
    }

    // Increment clientNo safely using mutex
    pthread_mutex_lock(&clientNoMutex);
    clientNo++;
    int currentClientNo = clientNo;
    pthread_mutex_unlock(&clientNoMutex);

    

    // Prepare the message with client's position
    memset(buffer,0,strlen(buffer));
    snprintf(buffer, MAX_BUFFER, "Client %d: Position (%d, %d)", currentClientNo, info->x, info->y);

    // If it's the first client, send the client count and PID
    if (currentClientNo == 1) {
        pid_t clientPid = getpid();
        printf("Client %d connected to the server\n", currentClientNo);
        if (send(sockfd, &info->clientCount, sizeof(info->clientCount), 0) < 0) {
            perror("Send clientCount failed");
            close(sockfd);
            pthread_exit(NULL);
        }

        if (send(sockfd, &clientPid, sizeof(clientPid), 0) < 0) {
            perror("Send clientPid failed");
            close(sockfd);
            pthread_exit(NULL);
        }

        
        if (send(sockfd, &info->townx, sizeof(info->townx), 0) < 0) {
            perror("Send coordinates failed");
            close(sockfd);
            pthread_exit(NULL);
        }

        if (send(sockfd, &info->towny, sizeof(info->towny), 0) < 0) {
            perror("Send coordinates failed");
            close(sockfd);
            pthread_exit(NULL);
        }
    }

    // Send the message to the server
    if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
        perror("Send message failed");
        close(sockfd);
        pthread_exit(NULL);
    }

    printf("Client %d sent message to the server: %s\n", currentClientNo, buffer);

    // Read response from the server
    for (int i = 0; i < 4; i++) {
        int n = read(sockfd, buffer, MAX_BUFFER - 1);

        if (n < 0) {
            perror("Read response failed");
            close(sockfd);
            pthread_exit(NULL);
        }

        buffer[n] = '\0'; 
        printf("Order %d status: %s\n", currentClientNo, buffer);
        if (i==3){
            pthread_mutex_lock(&deliveredPideMutex);
            deliverPide++;
            
            pthread_mutex_unlock(&deliveredPideMutex);
        }        
    }

    
    int finishFlag = 0;
    pthread_mutex_lock(&deliveredPideMutex);
    if (deliverPide == info->clientCount){
        finishFlag = 1;
        
    }
    pthread_mutex_unlock(&deliveredPideMutex);
    
    if (finishFlag) {
        printf("All customers served\nlog file written ..\n");
        
    }

    close(sockfd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s [portnumber] [numberOfClients] [p] [q]\n", argv[0]);
        exit(EXIT_FAILURE);
    } else if (atoi(argv[1]) <= 0 || atoi(argv[2]) <= 0 || atoi(argv[3]) <= 0 || atoi(argv[4]) <= 0) {
        fprintf(stderr, "Port number, number of clients, and town dimensions must be integers greater than 0\n");
        exit(EXIT_FAILURE);
    }
    printf("%d\n",getpid());
    int port = atoi(argv[1]);
    int numberOfClients = atoi(argv[2]);
    int p = atoi(argv[3]);
    int q = atoi(argv[4]);

    srand(time(NULL));
    pthread_t threads[numberOfClients];
    client_info_t clientInfos[numberOfClients];

    for (int i = 0; i < numberOfClients; i++) {
        clientInfos[i].clientId = i + 1;
        strcpy(clientInfos[i].hostname, "127.0.0.1");
        clientInfos[i].port = port;
        clientInfos[i].x = rand() % p;
        clientInfos[i].y = rand() % q;
        clientInfos[i].clientCount = numberOfClients;
        clientInfos[i].townx = p;
        clientInfos[i].towny = q;

        if (pthread_create(&threads[i], NULL, handle_client, &clientInfos[i]) != 0) {
            perror("Failed to create thread");
            exit(EXIT_FAILURE);
        }
        if (i==0)
            sleep(1);
    }

    for (int i = 0; i < numberOfClients; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
