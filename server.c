#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/ip.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <crypt.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>



#define EMPPATH "employees.txt"
#define CUSPATH "customers.txt"
#define JOINTPATH "jointHolders.txt"
#define LOANPATH "loanDetails.txt"
#define COUNTERPATH "loanCounter.txt"
#define HISTORYPATH "trans_hist.txt"
#define FEEDPATH "feedback.txt"
#define HASHKEY "$6$saltsalt$"



#define MAINMENU "\n--- Select Login Type ---\n1. Customer Login\t2. Joint Account Login\n3. Employee Login\t4. Manager Login\n5. Admin Login\t\t6. Exit Program\nEnter option: "
#define CUSMENU "\n--- Customer Panel ---\n1. Deposit Money\t2. Withdraw Money\n3. Balance Enquiry\t4. Apply for Loan\n5. Transfer Funds\t6. Change Password\n7. View Transaction History\t8. Provide Feedback\n9. View Details\t10. Logout\n11. Exit\nEnter option: "
#define ADMINMENU "\n--- Admin Console ---\n1. Register New Employee\t2. Add Customer Account\n3. Delete Customer Account\t4. Modify Customer/Employee Info\n5. Search Account\t6. Manage Roles and Permissions\n7. View Employee Details\t8. Logout\n9. Exit\nEnter option: "
#define EMPMENU "\n--- Employee Dashboard ---\n1. Register New Customer\t2. Edit Customer Information\n3. Review/Approve Loan Requests\t4. View Assigned Loan Tasks\n5. Review Customer Transactions\t6. Change Password\n7. Logout\t8. Exit\nEnter option: "
#define MNGMENU "\n--- Manager Section ---\n1. Enable/Disable Customer Accounts\t2. Assign Loan Applications to Employees\n3. View Customer Feedback\n4. Update Your Password\n5. Logout\t6. Exit\nEnter option: "


void employeeMenu(int connection_fd);
void managerMenu(int connection_fd);
void adminMenu(int connection_fd);
void addCustomer(int connection_fd);
void connectionHandler(int connection_fd);
void cleanupSemaphore(int signum);
void setupSignalHandlers();
void clearOldSessions();
void *reaperThread(void *arg);


#include "bank_fun.h"
#include "common_functions.h"
#include "customer_functions.h"
#include "admin_functions.h"
#include "employee_functions.h"
#include "manager_functions.h"


int activeClients = 0;                                    // number of connected clients
pthread_mutex_t countLock = PTHREAD_MUTEX_INITIALIZER;    // protects activeClients


// If the child process is killed (Ctrl+C, crash...), log the user out first
void cleanupSemaphore(int signum) {
    endSession();
    _exit(signum);
}

void setupSignalHandlers() {
    signal(SIGINT, cleanupSemaphore);
    signal(SIGTERM, cleanupSemaphore);
    signal(SIGSEGV, cleanupSemaphore);
    signal(SIGHUP, cleanupSemaphore);
    signal(SIGQUIT, cleanupSemaphore);
}

// When the server starts nobody is logged in yet. Remove login semaphores left over
// from an earlier run (for example if the server was killed with kill -9).
void clearOldSessions(){
    struct Customer c;
    struct JointHolder h;
    struct Employee e;
    char name[50];
    int file;

    file = open(CUSPATH, O_RDONLY);
    while(file != -1 && read(file, &c, sizeof(c)) == sizeof(c)){
        snprintf(name, sizeof(name), "/cus_%d", c.accountNumber);
        sem_unlink(name);
    }
    close(file);

    file = open(JOINTPATH, O_RDONLY);
    while(file != -1 && read(file, &h, sizeof(h)) == sizeof(h)){
        snprintf(name, sizeof(name), "/joint_%d", h.holderID);
        sem_unlink(name);
    }
    close(file);

    file = open(EMPPATH, O_RDONLY);
    while(file != -1 && read(file, &e, sizeof(e)) == sizeof(e)){
        snprintf(name, sizeof(name), "/emp_%d", e.empID);
        sem_unlink(name);
    }
    close(file);

    sem_unlink("/admin_0");
}

// Thread that runs inside the main server process.
// wait() blocks until a client's child process finishes, then we update the count.
// Without this, finished children would stay as zombie processes.
void *reaperThread(void *arg){
    while(1){
        pid_t pid = wait(NULL);
        if(pid > 0){
            pthread_mutex_lock(&countLock);
            activeClients--;
            printf("Client process %d finished. Active clients: %d\n", pid, activeClients);
            pthread_mutex_unlock(&countLock);
        }
        else{
            sleep(1);   // no child processes right now
        }
    }
    return NULL;
}

int main(){
    int socket_fd, connection_fd;
    int isBind, isListen;
    socklen_t clientSize;

    struct sockaddr_in address, client;

    // Print server logs immediately (no buffering), so parent and children don't mix up output
    setbuf(stdout, NULL);

    // Writing to a client that has disconnected should return an error, not kill the process
    signal(SIGPIPE, SIG_IGN);

    clearOldSessions();

    // Creating Socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_fd == -1){
        perror("Socket creation failed");
        exit(-1);
    }
    printf("Server Created\n");

    // Allows restarting the server immediately on the same port
    int opt = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);

    // Binding Socket
    isBind = bind(socket_fd, (struct sockaddr *)&address, sizeof(address));
    if (isBind == -1){
        perror("Binding failed");
        exit(-1);
    }
    printf("Binding successful!\n");

    // Listening for connection
    isListen = listen(socket_fd, 10);
    if (isListen == -1){
        perror("Error");
        exit(-1);
    }
    printf("Server listening for connections...\n");

    pthread_t tid;
    pthread_create(&tid, NULL, reaperThread, NULL);

    while(1){
        clientSize = sizeof(client);
        connection_fd = accept(socket_fd, (struct sockaddr *) &client, &clientSize);

        if (connection_fd == -1){
            perror("Accept Failed!");
            continue;
        }

        pid_t pid = fork();
        if(pid == 0){
            // Child process: serves only this one client
            close(socket_fd);
            setupSignalHandlers();
            connectionHandler(connection_fd);
            close(connection_fd);
            _exit(0);
        }
        else if(pid > 0){
            // Parent process: the child has its own copy of the socket, so close ours
            close(connection_fd);
            pthread_mutex_lock(&countLock);
            activeClients++;
            printf("New client process %d. Active clients: %d\n", pid, activeClients);
            pthread_mutex_unlock(&countLock);
        }
        else{
            perror("fork failed");
            close(connection_fd);
        }
    }
    close(socket_fd);

    return 0;
}

// Handling client connection
void connectionHandler(int connection_fd){
    int choice;

    while(1){
        askClient(connection_fd, MAINMENU);
        choice = atoi(readBuffer);
        printf("Client entered: %d\n", choice);

        switch (choice) {
            case 1:
                customerLogin(connection_fd);
                break;
            case 2:
                jointLogin(connection_fd);
                break;
            case 3:
                employeeMenu(connection_fd);
                break;
            case 4:
                managerMenu(connection_fd);
                break;
            case 5:
                adminMenu(connection_fd);
                break;
            case 6:
                exitClient(connection_fd);
                break;
            default:
                printf("Invalid input received in main menu\n");
                sendInfo(connection_fd, "Invalid Choice for Login menu");
        }
    }
}
