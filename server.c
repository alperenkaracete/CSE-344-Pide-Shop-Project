#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <math.h>


int totalPides;
int remainingPides = 100;
int availableCook;
int availableDeliveryPersonal;
int lastPides=0;
int totalDelivered=0;
int townx;
int towny;
int deliverySpeed;
int logFd;
int *cookPlace;
int *deliveryPlace;

#define BUFFER_SIZE 256

volatile sig_atomic_t exitFlag = 0;


typedef struct {
    int id;
    int clientSocketFd;
    char status[20];
    int x;
    int y;
} Pide;

pthread_mutex_t pide_queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cooked_queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t delivery_queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pide_ready = PTHREAD_COND_INITIALIZER;
pthread_cond_t delivery_ready = PTHREAD_COND_INITIALIZER;
pthread_cond_t finish = PTHREAD_COND_INITIALIZER;

Pide *pide_queue;
Pide *cooked_queue;
Pide *delivery_queue;
int pide_count = 0, cooked_count = 0, delivery_count = 0;
int oven_spots = 6;
int oven_aparatus = 3;
int oven_opennings = 2;
int delivery_capacity;
int cook_capacity;
int max_pides;

#define ROWS 30
#define COLS 40

typedef struct {
    double real;
    double imag;
} complex;

typedef struct {
    complex **data;
    int rows;
    int cols;
} matrix;


void allocateMatrix(matrix *mat, int rows, int cols) {
    mat->rows = rows;
    mat->cols = cols;
    mat->data = (complex **)malloc(rows * sizeof(complex *));
    if (mat->data == NULL) {
        perror("Error allocating memory for matrix data");
        exit(EXIT_FAILURE); // Handle allocation failure
    }
    for (int i = 0; i < rows; ++i) {
        mat->data[i] = (complex *)malloc(cols * sizeof(complex));
        if (mat->data[i] == NULL) {
            perror("Error allocating memory for matrix row");
            exit(EXIT_FAILURE); // Handle allocation failure
        }
    }
}

// Function to free memory allocated for a matrix
void freeMatrix(matrix *mat) {
    for (int i = 0; i < mat->rows; ++i) {
        free(mat->data[i]);
    }
    free(mat->data);
}

// Function to compute the conjugate transpose of a matrix
void conjugateTranspose(matrix *A, matrix *A_conj_trans) {
    int i, j;
    A_conj_trans->rows = A->cols;
    A_conj_trans->cols = A->rows;
    for (i = 0; i < A->rows; ++i) {
        for (j = 0; j < A->cols; ++j) {
            A_conj_trans->data[j][i].real = A->data[i][j].real;
            A_conj_trans->data[j][i].imag = -A->data[i][j].imag;
        }
    }
}

// Function to multiply two matrices
void matrixMultiply(matrix *A, matrix *B, matrix *result) {
    int i, j, k;
    result->rows = A->rows;
    result->cols = B->cols;
    for (i = 0; i < A->rows; ++i) {
        for (j = 0; j < B->cols; ++j) {
            result->data[i][j].real = 0.0;
            result->data[i][j].imag = 0.0;
            for (k = 0; k < A->cols; ++k) {
                result->data[i][j].real += A->data[i][k].real * B->data[k][j].real
                                         - A->data[i][k].imag * B->data[k][j].imag;
                result->data[i][j].imag += A->data[i][k].real * B->data[k][j].imag
                                         + A->data[i][k].imag * B->data[k][j].real;
            }
        }
    }
}

// Function to perform LU decomposition of a matrix
void luDecomp(matrix *A, matrix *L, matrix *U) {
    int i, j, k;
    for (i = 0; i < A->rows; ++i) {
        for (j = 0; j < A->cols; ++j) {
            if (j < i)
                U->data[j][i].real = 0.0;
            else {
                U->data[j][i] = A->data[j][i];
                for (k = 0; k < i; ++k) {
                    U->data[j][i].real -= L->data[j][k].real * U->data[k][i].real
                                        - L->data[j][k].imag * U->data[k][i].imag;
                    U->data[j][i].imag -= L->data[j][k].real * U->data[k][i].imag
                                        + L->data[j][k].imag * U->data[k][i].real;
                }
            }
        }
        for (j = 0; j < A->cols; ++j) {
            if (j < i)
                L->data[j][i].real = 0.0;
            else if (j == i)
                L->data[j][i].real = 1.0;
            else {
                L->data[j][i] = A->data[j][i];
                for (k = 0; k < i; ++k) {
                    L->data[j][i].real -= L->data[j][k].real * U->data[k][i].real
                                        - L->data[j][k].imag * U->data[k][i].imag;
                    L->data[j][i].imag -= L->data[j][k].real * U->data[k][i].imag
                                        + L->data[j][k].imag * U->data[k][i].real;
                }
                L->data[j][i].real /= U->data[i][i].real;
                L->data[j][i].imag /= U->data[i][i].real;
            }
        }
    }
}

// Function to solve a linear system using LU decomposition
void luSolve(matrix *L, matrix *U, matrix *B, matrix *X) {
    int i, j, k;

    // Initialize matrix Y with dimensions matching B
    matrix Y;
    Y.rows = B->rows;
    Y.cols = B->cols;
    Y.data = (complex **)malloc(Y.rows * sizeof(complex *));
    for (i = 0; i < Y.rows; ++i) {
        Y.data[i] = (complex *)malloc(Y.cols * sizeof(complex));
        for (j = 0; j < Y.cols; ++j) {
            Y.data[i][j].real = 0.0;
            Y.data[i][j].imag = 0.0;
        }
    }

    // Copy B into Y
    for (i = 0; i < B->rows; ++i) {
        for (j = 0; j < B->cols; ++j) {
            Y.data[i][j] = B->data[i][j];
            for (k = 0; k < i; ++k) {
                Y.data[i][j].real -= L->data[i][k].real * Y.data[k][j].real
                                   - L->data[i][k].imag * Y.data[k][j].imag;
                Y.data[i][j].imag -= L->data[i][k].real * Y.data[k][j].imag
                                   + L->data[i][k].imag * Y.data[k][j].real;
            }
        }
    }

    // Now proceed with solving using LU decomposition
    // ...

    // Free memory allocated for Y
    freeMatrix(&Y);
}

// Function to compute the pseudo-inverse of a matrix
void pseudoInverse(matrix *A, matrix *A_pinv) {
    matrix A_conj_trans;
    matrix AHA;
    matrix L, U;
    matrix I;
    matrix AHA_inv;
    matrix ATA_invAHA;

    allocateMatrix(&A_conj_trans, A->cols, A->rows);
    allocateMatrix(&AHA, A_conj_trans.rows, A->cols);
    allocateMatrix(&L, AHA.rows, AHA.cols);
    allocateMatrix(&U, AHA.rows, AHA.cols);
    allocateMatrix(&I, AHA.rows, AHA.cols);
    allocateMatrix(&AHA_inv, AHA.rows, AHA.cols);
    allocateMatrix(&ATA_invAHA, A->cols, A_conj_trans.cols);

    A_conj_trans.rows = A->cols;
    A_conj_trans.cols = A->rows;

    // Compute A^H (conjugate transpose of A)
    conjugateTranspose(A, &A_conj_trans);
    
    AHA.rows = A_conj_trans.rows;
    AHA.cols = A->cols;

    // Compute A^H * A
    matrixMultiply(&A_conj_trans, A, &AHA);

    
    L.rows = AHA.rows;
    L.cols = AHA.cols;
    U.rows = AHA.rows;
    U.cols = AHA.cols;

    // Perform LU decomposition of A^H * A
    luDecomp(&AHA, &L, &U);

    // Create identity matrix
    
    I.rows = AHA.rows;
    I.cols = AHA.cols;
    for (int i = 0; i < I.rows; ++i) {
        for (int j = 0; j < I.cols; ++j) {
            if (i == j) {
                I.data[i][j].real = 1.0;
                I.data[i][j].imag = 0.0;
            } else {
                I.data[i][j].real = 0.0;
                I.data[i][j].imag = 0.0;
            }
        }
    }

    
    AHA_inv.rows = AHA.rows;
    AHA_inv.cols = AHA.cols;

    // Solve for (A^H * A)^(-1)
    luSolve(&L, &U, &I, &AHA_inv);

    
    ATA_invAHA.rows = A->cols;
    ATA_invAHA.cols = A_conj_trans.cols;

    // Compute (A^H * A)^(-1) * A^H
    matrixMultiply(&AHA_inv, &A_conj_trans, &ATA_invAHA);

    // A_pinv = (A^H * A)^(-1) * A^H
    A_pinv->rows = ATA_invAHA.rows;
    A_pinv->cols = ATA_invAHA.cols;
    for (int i = 0; i < ATA_invAHA.rows; ++i) {
        for (int j = 0; j < ATA_invAHA.cols; ++j) {
            A_pinv->data[i][j].real = ATA_invAHA.data[i][j].real;
            A_pinv->data[i][j].imag = ATA_invAHA.data[i][j].imag;
        }
    }

    freeMatrix(&A_conj_trans);
    freeMatrix(&AHA);
    freeMatrix(&L);
    freeMatrix(&U);
    freeMatrix(&I);
    freeMatrix(&AHA_inv);
    freeMatrix(&ATA_invAHA);
}



void fillMatrixRandom(matrix *mat) {
    
    srand(time(NULL));

    for (int i = 0; i < mat->rows; ++i) {
        for (int j = 0; j < mat->cols; ++j) {
            mat->data[i][j].real = (double)rand() / RAND_MAX; // Random real part between 0 and 1
            mat->data[i][j].imag = (double)rand() / RAND_MAX; // Random imaginary part between 0 and 1
        }
    }
}


void sigIntHandler(int sig) {
    printf("SIGINT arrived exiting!\n");
    exitFlag = 1;
    pthread_cond_broadcast(&delivery_ready);
    pthread_cond_broadcast(&pide_ready);
    pthread_cond_broadcast(&finish);
}

struct serverClientStruct {
    int serverFd;
    struct sockaddr_in serverInfo;
    struct sockaddr_in clientInfo;
} information;

void* managerThreadFunc(void* arg) {
    int serverSocketFd = *(int*)arg;
    information.serverFd = serverSocketFd;
    socklen_t c_addrlen = sizeof(struct sockaddr_in);
    pid_t clientPid;
    int customerCount = 0;
    int totalPides;
    int start = 1;
    char log_buffer[256];

    while (!exitFlag) {
        int clientSocketFd = accept(serverSocketFd, (struct sockaddr*)&information.clientInfo, &c_addrlen);
        if (clientSocketFd == -1) {
            if (errno == EINTR && exitFlag) {
                break; // Exit if interrupted by SIGINT and exit flag is set
            }
            perror("Accept failed.");
            continue;
        }

        getpeername(clientSocketFd, (struct sockaddr*)&information.clientInfo, &c_addrlen);
        printf("Client %s:%d come in.\n", inet_ntoa(information.clientInfo.sin_addr), ntohs(information.clientInfo.sin_port));
        snprintf(log_buffer, BUFFER_SIZE, "Client %s:%d come in.\n", inet_ntoa(information.clientInfo.sin_addr), ntohs(information.clientInfo.sin_port));
        write(logFd, log_buffer, strlen(log_buffer));

        // Initial communication with the first client to get the count
        if (start) {
            start = 0;

            // Read customerCount from the client
            if (read(clientSocketFd, &customerCount, sizeof(customerCount)) < 0) {
                perror("ERROR reading customerCount from socket");
                close(clientSocketFd);
                exit(EXIT_FAILURE);
            }
            printf("%d new customers.. Serving\n", customerCount);
            snprintf(log_buffer, BUFFER_SIZE, "%d new customers.. Serving\n", customerCount);
            write(logFd, log_buffer, strlen(log_buffer));
            totalPides = customerCount;
            max_pides = totalPides;


            // Ensure max_pides is a reasonable number
            if (max_pides <= 0) {
                fprintf(stderr, "Invalid number of pides requested: %d\n", max_pides);
                close(clientSocketFd);
                continue;
            }

            pide_queue = malloc(max_pides * sizeof(Pide));
            cooked_queue = malloc(max_pides * sizeof(Pide));
            delivery_queue = malloc(max_pides * sizeof(Pide));

            if (!pide_queue || !cooked_queue || !delivery_queue) {
                perror("ERROR allocating memory for queues");
                close(clientSocketFd);
                free(pide_queue);
                free(cooked_queue);
                free(delivery_queue);
                break;
            }

            // Read clientPid from the client
            if (read(clientSocketFd, &clientPid, sizeof(clientPid)) < 0) {
                perror("ERROR reading clientPid from socket");
                close(clientSocketFd);
                free(pide_queue);
                free(cooked_queue);
                free(delivery_queue);
                continue;
            }

            int x,y;
            if (read(clientSocketFd, &x, sizeof(x)) < 0) {
                perror("ERROR reading clientPid from socket");
                close(clientSocketFd);
                free(pide_queue);
                free(cooked_queue);
                free(delivery_queue);
                continue;
            }        
            if (read(clientSocketFd, &y, sizeof(y)) < 0) {
                perror("ERROR reading clientPid from socket");
                close(clientSocketFd);
                free(pide_queue);
                free(cooked_queue);
                free(delivery_queue);
                continue;
            }                  
            townx = x;
            towny = y;
            
        }

        // Communicate with the client
        char buffer[256];
        int n = read(clientSocketFd, buffer, 255);
        int x, y;
        char *position_start = strstr(buffer, "Position (");

        
        if (n < 0) {
            perror("ERROR reading from socket");
        } else {

            if (position_start != NULL) {
                
                position_start += strlen("Position (");
                sscanf(position_start, "%d, %d", &x, &y);
            }             
            buffer[n] = '\0';
            
            pthread_mutex_lock(&pide_queue_lock);
            
            remainingPides = customerCount--;
            if (pide_count < max_pides) {
                Pide p = { .id = totalPides - customerCount, .clientSocketFd = clientSocketFd, .x = x, .y = y };
                pide_queue[pide_count++] = p;


                printf("Manager: Placed pide %d\n", p.id);
                snprintf(log_buffer, BUFFER_SIZE, "Manager: Placed pide order %d Coordinates: (%d, %d)\n", p.id, p.x, p.y);
                write(logFd, log_buffer, strlen(log_buffer));
                n = write(clientSocketFd, "Order Placed", 12);
                if (n < 0) {
                    perror("ERROR writing to socket");
                }
                pthread_cond_signal(&pide_ready);
            } else {
                printf("Error: pide_count (%d) >= max_pides (%d)\n", pide_count, max_pides);
            }
            pthread_mutex_unlock(&pide_queue_lock);
        }

        if (customerCount == 0) {
            
            // Read final message from client
            remainingPides = 0;
            lastPides = 1;
            pthread_cond_wait(&finish,&cooked_queue_lock);
            printf("Done serving client @ PID %d\n", clientPid);
            snprintf(log_buffer, BUFFER_SIZE, "Done serving client @ PID %d\n", clientPid);
            write(logFd, log_buffer, strlen(log_buffer));      
            int flagCook = 0;
            for (int i = 0; i < availableCook; i++) {
                for (int j = i + 1; j < availableCook; j++) {
                    if (cookPlace[j] > cookPlace[flagCook]) {
                        flagCook = j;
                    }
                }
            }
            printf("Thanks Cook %d and ", flagCook);

            int flagDelivery = 0;
            for (int i = 0; i < availableDeliveryPersonal; i++) {
                for (int j = i + 1; j < availableDeliveryPersonal; j++) {
                    if (deliveryPlace[j] > deliveryPlace[flagDelivery]) {
                        flagDelivery = j;
                    }
                }
            }
            printf("Moto %d \n", flagDelivery);

            snprintf(buffer, BUFFER_SIZE, "Thanks Cook %d for %d cooks and Moto %d for %d deliveries.\n", flagCook, cookPlace[flagCook], flagDelivery, deliveryPlace[flagDelivery]);
            write(logFd, buffer, strlen(buffer));
            printf("active waiting for connections\n");
            snprintf(buffer,BUFFER_SIZE,"active waiting for connections\n");
            write(logFd, buffer, strlen(buffer));
            start = 1;
            cooked_count = 0;
            delivery_count = 0;
            totalDelivered = 0;
            pide_count = 0;
            free(pide_queue);
            free(cooked_queue);
            free(delivery_queue);
        }
    }
    
    pthread_exit(NULL);
}

void* cookThreadFunc(void* arg) {
    int cook_id = *(int*)arg;
    free(arg);
    matrix mat, matPinv;
    allocateMatrix(&mat, ROWS, COLS);
    allocateMatrix(&matPinv, COLS, ROWS);
    fillMatrixRandom(&mat);
    char log_buffer[256];

    while (!exitFlag) {
        pthread_mutex_lock(&pide_queue_lock);
        while (pide_count == 0 && !exitFlag) {
            pthread_cond_wait(&pide_ready, &pide_queue_lock);
        }
        if (exitFlag) {
            pthread_mutex_unlock(&pide_queue_lock);
            break; 
        }

        Pide p = pide_queue[--pide_count];
        pthread_mutex_unlock(&pide_queue_lock);

        double executionTime;
        clock_t start = clock();
        pseudoInverse(&mat, &matPinv);
        clock_t end = clock();
        executionTime = ((double)(end - start)) / CLOCKS_PER_SEC;
        strcpy(p.status, "Prepared");
        //printf("Cook %d: Prepared pide %d\n", cook_id, p.id);
        cookPlace[cook_id]++;
        int n = write(p.clientSocketFd, "Order prepared", 14);
        if (n < 0) {
            perror("ERROR writing to socket");
        }
        snprintf(log_buffer, sizeof(log_buffer), "Cook %d: Prepared pide %d\n", cook_id, p.id);
        write(logFd, log_buffer, strlen(log_buffer));

        pthread_mutex_lock(&cooked_queue_lock);
        while ((oven_spots == 0 || oven_opennings == 0 || oven_aparatus==0) && !exitFlag) {
                pthread_cond_wait(&delivery_ready, &cooked_queue_lock);
            }
        if (exitFlag) {
            pthread_mutex_unlock(&cooked_queue_lock);
            break; 
        }

        oven_spots--;
        oven_opennings--;
        oven_aparatus--;
        pthread_mutex_unlock(&cooked_queue_lock);

        sleep(executionTime / 2);

        pthread_mutex_lock(&cooked_queue_lock);
        
        oven_spots++;
        oven_aparatus++;
        oven_opennings++;
        strcpy(p.status, "Cooked");
        cooked_queue[cooked_count++] = p;
        //printf("Cook %d: Cooked pide %d\n", cook_id, p.id);
        snprintf(log_buffer, sizeof(log_buffer), "Cook %d: Cooked pide %d\n", cook_id, p.id);
        write(logFd, log_buffer, strlen(log_buffer));
        n = write(p.clientSocketFd, "Order cooked", 12);
        if (n < 0) {
            perror("ERROR writing to socket");
        }
        pthread_cond_signal(&delivery_ready);
        pthread_mutex_unlock(&cooked_queue_lock);
    }

    freeMatrix(&mat);
    freeMatrix(&matPinv);
    pthread_exit(NULL);
}


void* deliveryThreadFunc(void* arg) {
    int delivery_id = *(int*)arg;
    free(arg);
    char temp[100];
    while (!exitFlag) {
        pthread_mutex_lock(&cooked_queue_lock);
        
        while (cooked_count < 3 && !exitFlag) {
            
            pthread_cond_wait(&delivery_ready, &cooked_queue_lock);

            if (max_pides-totalDelivered < 3){
                break;
            }
            
        }
        if (exitFlag){
            
            pthread_mutex_unlock(&cooked_queue_lock);
            break;
        }  
        
        if (delivery_count > 0 && cooked_count != 3) {
            
            while ((cooked_count > 0 && delivery_count != 3) && !exitFlag) {
                delivery_queue[delivery_count++] = cooked_queue[--cooked_count];
                if (exitFlag)
                    break;
            }
            if (exitFlag) {
                
                pthread_mutex_unlock(&cooked_queue_lock);
                break; 
            }
            for (int i = 0; i < delivery_count; i++) {
                Pide p = delivery_queue[i];
                strcpy(p.status,"Delivered");
                //printf("Delivery %d: Delivered pide %d\n", delivery_id, p.id);
                int distance = sqrt(pow(p.x - townx/2, 2) + pow(p.y - towny/2, 2));
                sleep(distance/deliverySpeed);
                snprintf(temp, sizeof(temp), "Delivery %d: Delivered pide %d", delivery_id, p.id);
                int n = write(p.clientSocketFd, temp, strlen(temp));
                if (n < 0) {
                    perror("ERROR writing to socket");
                } 
                snprintf(temp, sizeof(temp), "Delivery %d: Delivered pide %d\n", delivery_id, p.id);                       
                write(logFd, temp, strlen(temp));
                if (n < 0) {
                    perror("ERROR writing to socket");
                }                   
                close(p.clientSocketFd);
 
            }
            delivery_count = 0;
            deliveryPlace[delivery_id]++;
            pthread_mutex_unlock(&cooked_queue_lock);
        }

        else {
            
            while ((cooked_count != 0) && !exitFlag) {
                delivery_queue[delivery_count++] = cooked_queue[--cooked_count];

            }
            if (exitFlag) {
                
                break; 
            }
            if (delivery_count > 0) {
                for (int i = 0; i < delivery_count; i++) {
                    Pide p = delivery_queue[i];
                    strcpy(p.status, "Delivered");
                    //printf("Delivery %d: Delivered pide %d\n", delivery_id, p.id);
                    
                    int distance = sqrt(pow(p.x - townx/2, 2) + pow(p.y - towny/2, 2));
                    sleep(distance/deliverySpeed);
                    snprintf(temp, sizeof(temp), "Delivery %d: Delivered pide %d", delivery_id, p.id);
                    int n = write(p.clientSocketFd, temp, strlen(temp));
                    if (n < 0) {
                        perror("ERROR writing to socket");
                    } 
                    snprintf(temp, sizeof(temp), "Delivery %d: Delivered pide %d\n", delivery_id, p.id);                       
                    write(logFd, temp, strlen(temp));
                    
                    close(p.clientSocketFd);
                    totalDelivered++;
                    
                }
                delivery_count = 0;
                deliveryPlace[delivery_id]++;
                if (max_pides-totalDelivered < 3 && max_pides-totalDelivered > 0){
                    pthread_cond_signal(&delivery_ready);    
                     
                }                
            }    

            pthread_mutex_unlock(&cooked_queue_lock);
                 
        }            
        if (max_pides - totalDelivered == 0){
            
            pthread_cond_signal(&finish);
        }

    }
    
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    int logFileFd = open("server.log", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (logFileFd == -1) {
        perror("Error opening log file.");
        exit(EXIT_FAILURE);
    }
    logFd = logFileFd;
    struct sigaction sa;
    sa.sa_handler = sigIntHandler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error sigaction:");
        exit(EXIT_FAILURE);
    }

    if (argc != 5) {
        printf("Wrong arguments!\nUsage Example: ./PideShop [portnumber] [CookthreadPoolSize] [DeliveryPoolSize] [k]\n");
        return 1;
    } else if (atoi(argv[1]) == 0 || atoi(argv[2]) == 0 || atoi(argv[3]) == 0 || atoi(argv[4]) == 0) {
        printf("2nd, 3rd, 4th, and 5th arguments should be integers!\nUsage Example: ./PideShop [portnumber] [CookthreadPoolSize] [DeliveryPoolSize] [k]\n");
        return 1;
    }


    printf("PideShop active waitng for connection\n");
    
    int port = atoi(argv[1]),
    cookThreadPoolSize = atoi(argv[2]),
    deliveryThreadPoolSize = atoi(argv[3]);
    deliverySpeed = atoi(argv[4]);
    availableCook = cookThreadPoolSize;
    availableDeliveryPersonal = deliveryThreadPoolSize;

    pthread_t* cookThreads = malloc(cookThreadPoolSize * sizeof(pthread_t));
    pthread_t* deliveryThreads = malloc(deliveryThreadPoolSize * sizeof(pthread_t));

    deliveryPlace = (int*) malloc(sizeof(int)*deliveryThreadPoolSize);
    cookPlace = (int*) malloc(sizeof(int)*cookThreadPoolSize);

    // Create socket
    int serverSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketFd == -1) {
        perror("Failed to create the server socket.");
        exit(EXIT_FAILURE);
    }

    // Socket information
    struct sockaddr_in serverInfo;
    socklen_t s_addrlen = sizeof(serverInfo);
    memset(&serverInfo, 0, s_addrlen);
    serverInfo.sin_family = AF_INET;
    serverInfo.sin_addr.s_addr = INADDR_ANY;
    serverInfo.sin_port = htons(port);

    if (bind(serverSocketFd, (struct sockaddr *)&serverInfo, s_addrlen) == -1) {
        perror("Bind failed.");
        close(serverSocketFd);
        exit(EXIT_FAILURE);
    }

    if (listen(serverSocketFd, 5) == -1) {
        perror("Listen failed.");
        close(serverSocketFd);
        exit(EXIT_FAILURE);
    }

    // Print Server clientPid
    getsockname(serverSocketFd, (struct sockaddr*)&serverInfo, &s_addrlen);
    printf("Start Server on: %s:%d\n", inet_ntoa(serverInfo.sin_addr), ntohs(serverInfo.sin_port));

    pthread_t manager;
    if (pthread_create(&manager, NULL, managerThreadFunc, (void *)&serverSocketFd) != 0) {
        perror("Create pthread error!");
        exit(EXIT_FAILURE);
    }

    
    for (int i = 0; i < cookThreadPoolSize; i++) {
        int* cookId = malloc(sizeof(int));
        if (cookId == NULL) {
            perror("Memory allocation error!");
            exit(EXIT_FAILURE);
        }
        *cookId = i;
        if (pthread_create(&cookThreads[i], NULL, cookThreadFunc, cookId) != 0) {
            perror("Create cook thread error!");
            exit(EXIT_FAILURE);
        }
    }

    
    for (int i = 0; i < deliveryThreadPoolSize; i++) {
        int* deliveryId = malloc(sizeof(int));
        if (deliveryId == NULL) {
            perror("Memory allocation error!");
            exit(EXIT_FAILURE);
        }
        *deliveryId = i;
        if (pthread_create(&deliveryThreads[i], NULL, deliveryThreadFunc, deliveryId) != 0) {
            perror("Create delivery thread error!");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for the manager thread to finish
    pthread_join(manager, NULL);

    // Wait for all cook and delivery threads to finish
    for (int i = 0; i < cookThreadPoolSize; i++) {
        pthread_join(cookThreads[i], NULL);
    }

    for (int i = 0; i < deliveryThreadPoolSize; i++) {
        pthread_join(deliveryThreads[i], NULL);
    }

    free(cookThreads);
    free(deliveryThreads);
    free(pide_queue);
    free(cooked_queue);
    free(delivery_queue);
    free(deliveryPlace);
    free(cookPlace);

    if (close(logFileFd) == -1) {
        perror("Cannot close log file:");
    }

    close(serverSocketFd);

    return 0;
}
