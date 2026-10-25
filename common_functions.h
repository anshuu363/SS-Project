// ======================= Common helpers used by every menu =======================

char readBuffer[4096], write_buf[4096];

sem_t *sessionSem = NULL;   // semaphore of the user logged in on this connection
char sessionName[50];       // its name, e.g. "/cus_101"


// ======================= Session (one login per user) =======================
// Every user has a named semaphore with value 1 (= free).
// Login does sem_trywait: it succeeds only if the value is 1 and makes it 0.
// A second session of the same user finds value 0, so sem_trywait fails -> login refused.
// prefix keeps the user types apart: "cus", "joint", "emp", "admin".
int startSession(char *prefix, int id){
    snprintf(sessionName, sizeof(sessionName), "/%s_%d", prefix, id);
    sessionSem = sem_open(sessionName, O_CREAT, 0644, 1);
    if(sessionSem == SEM_FAILED){
        perror("sem_open");
        sessionSem = NULL;
        return 0;
    }
    if(sem_trywait(sessionSem) == -1){
        printf("%s is already logged in!\n", sessionName);
        sem_close(sessionSem);
        sessionSem = NULL;
        return 0;
    }
    return 1;
}

// Logout: give the semaphore back (value 0 -> 1) so the user can log in again
void endSession(){
    if(sessionSem != NULL){
        sem_post(sessionSem);
        sem_close(sessionSem);
        sessionSem = NULL;
        printf("%s logged out\n", sessionName);
    }
}


// ======================= Talking to the client =======================
// Read the client's reply. read() returns 0 (or -1) when the client has disconnected,
// e.g. pressed Ctrl+C. Then we free the user's login and end this child process.
int readClient(int connection_fd, char *buffer, int size){
    int n = read(connection_fd, buffer, size);
    if(n <= 0){
        printf("Client disconnected\n");
        endSession();
        close(connection_fd);
        _exit(0);
    }
    return n;
}

// Send a question and read the answer into readBuffer
void askClient(int connection_fd, char *prompt){
    bzero(write_buf, sizeof(write_buf));
    strncpy(write_buf, prompt, sizeof(write_buf) - 1);
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    readClient(connection_fd, readBuffer, sizeof(readBuffer));
}

// Send a message that needs no input. The '^' at the end tells the client
// to only print it and send back an empty reply.
void sendInfo(int connection_fd, char *message){
    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "%s^", message);
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    readClient(connection_fd, readBuffer, sizeof(readBuffer));
}

// Copy the client's answer into a smaller array without overflowing it
void copyInput(char *dest, int size){
    strncpy(dest, readBuffer, size - 1);
    dest[size - 1] = '\0';
}

void logout(int connection_fd){
    endSession();
    sendInfo(connection_fd, "Logged out");
}

void exitClient(int connection_fd){
    endSession();
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Client logging out...\n");
    write(connection_fd, write_buf, sizeof(write_buf));
    close(connection_fd);
    _exit(0);   // end this child process
}


// ======================= File locking =======================
// Lock or unlock one record of a file.
// type = F_RDLCK (read lock), F_WRLCK (write lock) or F_UNLCK (unlock).
// size = 0 means "till the end of the file", so (0, 0) locks the whole file.
// F_SETLKW waits if another process holds a conflicting lock.
int lockRecord(int file, int type, int offset, int size){
    struct flock fl;
    fl.l_type = type;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = size;
    fl.l_pid = getpid();

    int result = fcntl(file, F_SETLKW, &fl);
    if(result == -1)
        perror("fcntl");
    return result;
}


// ======================= Searching records =======================
// Each function returns the offset of the record in the file (and fills the struct),
// or -1 if it is not found.
int findCustomer(int file, int accountNumber, struct Customer *c){
    int offset = 0;
    lseek(file, 0, SEEK_SET);
    while(read(file, c, sizeof(struct Customer)) == sizeof(struct Customer)){
        if(c->accountNumber == accountNumber)
            return offset;
        offset += sizeof(struct Customer);
    }
    return -1;
}

int findJointHolder(int file, int holderID, struct JointHolder *h){
    int offset = 0;
    lseek(file, 0, SEEK_SET);
    while(read(file, h, sizeof(struct JointHolder)) == sizeof(struct JointHolder)){
        if(h->holderID == holderID)
            return offset;
        offset += sizeof(struct JointHolder);
    }
    return -1;
}

int findEmployee(int file, int empID, struct Employee *e){
    int offset = 0;
    lseek(file, 0, SEEK_SET);
    while(read(file, e, sizeof(struct Employee)) == sizeof(struct Employee)){
        if(e->empID == empID)
            return offset;
        offset += sizeof(struct Employee);
    }
    return -1;
}

int findLoan(int file, int loanID, struct LoanDetails *ld){
    int offset = 0;
    lseek(file, 0, SEEK_SET);
    while(read(file, ld, sizeof(struct LoanDetails)) == sizeof(struct LoanDetails)){
        if(ld->loanID == loanID)
            return offset;
        offset += sizeof(struct LoanDetails);
    }
    return -1;
}

// Find a customer, lock the record and read it again, because it may have
// changed while we were waiting for the lock.
// lockType: F_RDLCK to only read it, F_WRLCK to change it.
// Returns the offset, or -1 if the account does not exist (or was deleted meanwhile).
int lockCustomer(int file, int accountNumber, int lockType, struct Customer *c){
    int offset = findCustomer(file, accountNumber, c);
    if(offset == -1)
        return -1;

    lockRecord(file, lockType, offset, sizeof(struct Customer));
    lseek(file, offset, SEEK_SET);
    read(file, c, sizeof(struct Customer));

    if(c->accountNumber != accountNumber){
        lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
        return -1;
    }
    return offset;
}

// Write a customer record back at its offset
void saveCustomer(int file, int offset, struct Customer *c){
    lseek(file, offset, SEEK_SET);
    write(file, c, sizeof(struct Customer));
}


// ======================= Transaction history =======================
// Append one line, e.g. "500.00 deposited at 10:20:30 2024-10-17"
void addHistory(int accountNumber, char *text){
    struct trans_histroy th;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    bzero(&th, sizeof(th));
    th.acc_no = accountNumber;
    snprintf(th.hist, sizeof(th.hist), "%s at %02d:%02d:%02d %d-%d-%d\n", text,
             t->tm_hour, t->tm_min, t->tm_sec, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

    int fp = open(HISTORYPATH, O_WRONLY | O_APPEND | O_CREAT, 0644);
    write(fp, &th, sizeof(th));
    close(fp);
}
