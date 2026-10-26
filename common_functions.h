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

    if(lockRecord(file, lockType, offset, sizeof(struct Customer)) == -1)
        return -1;
    lseek(file, offset, SEEK_SET);
    read(file, c, sizeof(struct Customer));

    if(c->accountNumber != accountNumber){
        lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
        return -1;
    }
    return offset;
}

// ======================= Saving records (Durability) =======================
// Write one record at offset (-1 = at the end of the file), then fsync it, so the data
// is really on the disk (not only in the OS cache) before we tell the user "done".
// Returns 1 if it worked, 0 if not.
int saveRecord(int file, int offset, void *data, int size){
    if(offset == -1)
        offset = lseek(file, 0, SEEK_END);
    if(lseek(file, offset, SEEK_SET) == -1 || write(file, data, size) != size){
        perror("write");
        return 0;
    }
    if(fsync(file) == -1){
        perror("fsync");
        return 0;
    }
    return 1;
}

// Write a customer record back at its offset
int saveCustomer(int file, int offset, struct Customer *c){
    return saveRecord(file, offset, c, sizeof(struct Customer));
}


// ======================= Transaction history =======================
// Fill one line, e.g. "500.00 deposited at 10:20:30 2024-10-17"
void makeHistory(struct trans_histroy *th, int accountNumber, char *text){
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    bzero(th, sizeof(*th));
    th->acc_no = accountNumber;
    snprintf(th->hist, sizeof(th->hist), "%s at %02d:%02d:%02d %d-%d-%d\n", text,
             t->tm_hour, t->tm_min, t->tm_sec, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
}

int appendHistory(struct trans_histroy *th){
    int fp = open(HISTORYPATH, O_WRONLY | O_APPEND | O_CREAT, 0644);
    int ok = fp != -1 && saveRecord(fp, -1, th, sizeof(*th));
    close(fp);
    return ok;
}

// Is exactly this line (same text and time) already in the history file?
int historyExists(struct trans_histroy *th){
    struct trans_histroy t;
    int found = 0;
    int fp = open(HISTORYPATH, O_RDONLY | O_CREAT, 0644);
    while(!found && read(fp, &t, sizeof(t)) == sizeof(t))
        found = memcmp(&t, th, sizeof(t)) == 0;
    close(fp);
    return found;
}


// ======================= Journal (Atomicity) =======================
// One operation often changes more than one record: a transfer changes two balances,
// a loan approval changes a balance and the loan status. If the server crashes between
// those writes, money would be lost or created (or a loan could be paid out twice).
//
// So before changing anything we write the whole plan into journal.txt and fsync it:
// for every record its file, its offset, the old bytes and the new bytes, plus the
// history lines. Then we make the changes and mark the entry as done.
// When the server starts, recoverJournal() finishes every entry that is not done.
// So an operation happens completely or not at all.

#define MAXWRITES 4      // most records one operation changes (joint account: customer + 2 holders)
#define MAXHIST 2        // most history lines of one operation (transfer: one per account)
#define RECSIZE 512      // bigger than every record struct

enum { CUSFILE, JOINTFILE, LOANFILE };                   // which file a journal write goes to
char *journalFiles[] = { CUSPATH, JOINTPATH, LOANPATH };

struct JournalWrite{
    int fileID, fd, offset, size;   // fd is only used by the process that made the entry
    char before[RECSIZE], after[RECSIZE];
};

struct JournalEntry{
    int done;                       // must stay the first field: 0 = not finished, 1 = finished
    int writes, histories;
    struct JournalWrite w[MAXWRITES];
    struct trans_histroy hist[MAXHIST];
};

void startJournal(struct JournalEntry *j){
    bzero(j, sizeof(*j));
}

// Add one record change. fd must be the descriptor the caller holds the lock on.
// before = NULL for a new record that is added at the end of the file.
void journalWrite(struct JournalEntry *j, int fileID, int fd, int offset, void *before, void *after, int size){
    struct JournalWrite *w = &j->w[j->writes++];
    w->fileID = fileID;
    w->fd = fd;
    w->offset = offset;
    w->size = size;
    if(before != NULL)
        memcpy(w->before, before, size);
    memcpy(w->after, after, size);
}

void journalHistory(struct JournalEntry *j, int accountNumber, char *text){
    makeHistory(&j->hist[j->histories++], accountNumber, text);
}

// Block the signals that would end this process, so Ctrl+C or kill cannot stop us
// between two writes of one operation. A blocked signal is handled after unblocking.
void blockSignals(int block){
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGQUIT);
    sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL);
}

// Make all changes of the entry. The caller must hold write locks on all these records.
// Returns 1 if the changes were saved, 0 if nothing was changed.
int commitJournal(struct JournalEntry *j){
    int jfile = open(JOURNALPATH, O_RDWR | O_CREAT, 0644);
    if(jfile == -1){
        perror("journal");
        return 0;
    }
    blockSignals(1);

    // 1. Save the plan. Other processes add entries too, so lock the journal meanwhile.
    int ok = lockRecord(jfile, F_WRLCK, 0, 0) != -1;
    int jOffset = lseek(jfile, 0, SEEK_END);
    ok = ok && saveRecord(jfile, jOffset, j, sizeof(*j));
    lockRecord(jfile, F_UNLCK, 0, 0);

    // 2. Change the records. If one write fails, put the old values back.
    int changed = 0;
    while(ok && changed < j->writes){
        struct JournalWrite *w = &j->w[changed];
        ok = saveRecord(w->fd, w->offset, w->after, w->size);
        if(ok)
            changed++;
    }
    if(!ok){
        while(changed-- > 0)
            saveRecord(j->w[changed].fd, j->w[changed].offset, j->w[changed].before, j->w[changed].size);
    }

    // 3. Add the history lines. If this fails the entry stays open and recovery adds them later.
    int histOK = 1, i;
    for(i = 0; ok && histOK && i < j->histories; i++)
        histOK = appendHistory(&j->hist[i]);

    // 4. Mark the entry as done
    int done = 1;
    if(histOK)
        saveRecord(jfile, jOffset, &done, sizeof(done));

    close(jfile);
    blockSignals(0);
    return ok;
}

// Called once when the server starts, before any client is connected.
// Finishes every operation that was interrupted by a crash.
void recoverJournal(){
    struct JournalEntry j;
    char current[RECSIZE];
    int i, recovered = 0;

    int jfile = open(JOURNALPATH, O_RDWR | O_CREAT, 0644);
    if(jfile == -1)
        return;

    while(read(jfile, &j, sizeof(j)) == sizeof(j)){
        if(j.done)
            continue;

        for(i = 0; i < j.writes; i++){
            struct JournalWrite *w = &j.w[i];
            int file = open(journalFiles[w->fileID], O_RDWR | O_CREAT, 0644);
            bzero(current, sizeof(current));
            int n = pread(file, current, w->size, w->offset);

            // Only write the new value if the record still has the old one (or does not
            // exist yet). If it already has the new value, that part was done before the crash.
            if(n < w->size || memcmp(current, w->before, w->size) == 0)
                saveRecord(file, w->offset, w->after, w->size);
            close(file);
        }
        for(i = 0; i < j.histories; i++)
            if(!historyExists(&j.hist[i]))
                appendHistory(&j.hist[i]);
        recovered++;
    }

    // Everything is finished now, so the journal can start empty
    ftruncate(jfile, 0);
    fsync(jfile);
    close(jfile);

    if(recovered > 0)
        printf("Recovered %d unfinished operations from the journal\n", recovered);
}
