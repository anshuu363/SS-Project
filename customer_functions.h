void customerLogin(int connection_fd);
void jointLogin(int connection_fd);
void customerMenu(int connection_fd, int accountNumber, int holderID);
void depositMoney(int connection_fd, int accountNumber);
void withdrawMoney(int connection_fd, int accountNumber);
void customerBal(int connection_fd, int accountNumber);
void applyLoan(int connection_fd, int accountNumber);
void transferFunds(int connection_fd, int sourceAccount);
void changePassword(int connection_fd, int accountNumber);
void changeJointPassword(int connection_fd, int holderID);
void transactionHistory(int connection_fd, int accountNumber);
void addFeedback(int connection_fd);
void viewDetails(int connection_fd, int accountNumber);


// ======================= Normal customer login =======================
void customerLogin(int connection_fd){
    struct Customer c;
    int accountNumber;
    char password[256];

    askClient(connection_fd, "\nEnter account number: ");
    accountNumber = atoi(readBuffer);

    askClient(connection_fd, "Enter password: ");
    copyInput(password, sizeof(password));

    int file = open(CUSPATH, O_RDONLY | O_CREAT, 0644);
    int offset = findCustomer(file, accountNumber, &c);
    close(file);

    // A joint account has an empty password here, so it can only be used through "Joint Account Login"
    if(offset == -1 || c.activeStatus != 1 || strcmp(c.password, crypt(password, HASHKEY)) != 0){
        sendInfo(connection_fd, "\nInvalid account number or password");
        return;
    }

    if(!startSession("cus", accountNumber)){
        sendInfo(connection_fd, "\nThis account is already logged in from another session");
        return;
    }

    printf("Customer whose acc no.: %d loggedIn\n", accountNumber);
    sendInfo(connection_fd, "\nLogin Successfully");
    customerMenu(connection_fd, accountNumber, -1);   // -1 = not a joint holder
}

// ======================= Joint account login =======================
// Each holder logs in with an own holder ID. Two holders can be logged in at the same
// time on the same account, so deposits/withdrawals use write locks and viewing uses read locks.
void jointLogin(int connection_fd){
    struct JointHolder h;
    struct Customer c;
    int holderID;
    char password[256];

    askClient(connection_fd, "\nEnter joint holder ID: ");
    holderID = atoi(readBuffer);

    askClient(connection_fd, "Enter password: ");
    copyInput(password, sizeof(password));

    int file = open(JOINTPATH, O_RDONLY | O_CREAT, 0644);
    int offset = findJointHolder(file, holderID, &h);
    close(file);

    if(offset == -1 || strcmp(h.password, crypt(password, HASHKEY)) != 0){
        sendInfo(connection_fd, "\nInvalid holder ID or password");
        return;
    }

    // The shared account must exist and be active
    file = open(CUSPATH, O_RDONLY | O_CREAT, 0644);
    offset = findCustomer(file, h.accountNumber, &c);
    close(file);
    if(offset == -1 || c.activeStatus != 1){
        sendInfo(connection_fd, "\nThis joint account is deactivated");
        return;
    }

    if(!startSession("joint", holderID)){
        sendInfo(connection_fd, "\nThis holder is already logged in from another session");
        return;
    }

    printf("Joint holder %d of acc no.: %d loggedIn\n", holderID, h.accountNumber);
    sendInfo(connection_fd, "\nLogin Successfully");
    customerMenu(connection_fd, h.accountNumber, holderID);
}

// ======================= Customer menu (normal and joint) =======================
void customerMenu(int connection_fd, int accountNumber, int holderID){
    int choice;

    while(1){
        askClient(connection_fd, CUSMENU);
        choice = atoi(readBuffer);
        printf("Customer %d entered: %d\n", accountNumber, choice);

        switch(choice){
            case 1:
                depositMoney(connection_fd, accountNumber);
                break;
            case 2:
                withdrawMoney(connection_fd, accountNumber);
                break;
            case 3:
                customerBal(connection_fd, accountNumber);
                break;
            case 4:
                applyLoan(connection_fd, accountNumber);
                break;
            case 5:
                transferFunds(connection_fd, accountNumber);
                break;
            case 6:
                if(holderID == -1)
                    changePassword(connection_fd, accountNumber);
                else
                    changeJointPassword(connection_fd, holderID);
                break;
            case 7:
                transactionHistory(connection_fd, accountNumber);
                break;
            case 8:
                addFeedback(connection_fd);
                break;
            case 9:
                viewDetails(connection_fd, accountNumber);
                break;
            case 10:
                logout(connection_fd);
                return;
            case 11:
                exitClient(connection_fd);
                break;
            default:
                sendInfo(connection_fd, "Invalid Choice for customer menu");
        }
    }
}

// ======================= Deposit Money =======================
void depositMoney(int connection_fd, int accountNumber){
    struct Customer c;
    char text[100];

    askClient(connection_fd, "Enter the amount to deposit: ");
    float amount = atof(readBuffer);
    if(amount <= 0){
        sendInfo(connection_fd, "Amount must be greater than 0");
        return;
    }

    int file = open(CUSPATH, O_RDWR);
    // Write lock: no other process can read or change this account until we unlock
    int offset = lockCustomer(file, accountNumber, F_WRLCK, &c);
    if(offset == -1){
        close(file);
        sendInfo(connection_fd, "Account not found");
        return;
    }

    // The account may have been deactivated by the manager after this customer logged in
    int ok = c.activeStatus == 1;
    if(ok){
        // New balance and history line are saved together through the journal
        struct JournalEntry j;
        struct Customer old = c;
        startJournal(&j);
        c.balance += amount;
        journalWrite(&j, CUSFILE, file, offset, &old, &c, sizeof(c));
        sprintf(text, "%.2f deposited", amount);
        journalHistory(&j, accountNumber, text);
        ok = commitJournal(&j);
    }

    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

    if(!ok){
        sendInfo(connection_fd, c.activeStatus != 1 ? "This account is deactivated" : "Deposit failed, please try again");
        return;
    }
    printf("Customer whose acc no.: %d deposited %.2f\n", accountNumber, amount);
    sendInfo(connection_fd, "Deposit successful!");
}

// ======================= Withdraw Money =======================
void withdrawMoney(int connection_fd, int accountNumber){
    struct Customer c;
    char text[100];

    askClient(connection_fd, "Enter the amount to withdraw: ");
    float amount = atof(readBuffer);
    if(amount <= 0){
        sendInfo(connection_fd, "Amount must be greater than 0");
        return;
    }

    int file = open(CUSPATH, O_RDWR);
    int offset = lockCustomer(file, accountNumber, F_WRLCK, &c);
    if(offset == -1){
        close(file);
        sendInfo(connection_fd, "Account not found");
        return;
    }

    // Balance is checked while we hold the lock, so two joint holders
    // can never both withdraw the same money
    char *message = "Withdrawal successful!";
    if(c.activeStatus != 1)
        message = "This account is deactivated";
    else if(c.balance < amount){
        printf("Insufficient balance in account: %d\n", accountNumber);
        message = "Insufficient funds!";
    }
    else{
        struct JournalEntry j;
        struct Customer old = c;
        startJournal(&j);
        c.balance -= amount;
        journalWrite(&j, CUSFILE, file, offset, &old, &c, sizeof(c));
        sprintf(text, "%.2f withdraw", amount);
        journalHistory(&j, accountNumber, text);
        if(commitJournal(&j))
            printf("New balance of %d account number: %.2f\n", accountNumber, c.balance);
        else
            message = "Withdrawal failed, please try again";
    }

    // Unlock before talking to the client, so others never wait for this client's network
    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);
    sendInfo(connection_fd, message);
}

// ======================= View Balance =======================
void customerBal(int connection_fd, int accountNumber){
    struct Customer c;
    char message[100];

    int file = open(CUSPATH, O_RDONLY);
    // Read lock: other readers are allowed, but nobody can write while we read
    int offset = lockCustomer(file, accountNumber, F_RDLCK, &c);
    if(offset == -1){
        close(file);
        sendInfo(connection_fd, "Account not found");
        return;
    }
    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

    sprintf(message, "The current balance is: %.2f", c.balance);
    sendInfo(connection_fd, message);
}

// ======================= View Details =======================
// Also used by the admin's "Search Account"
void viewDetails(int connection_fd, int accountNumber){
    struct Customer c;
    struct JointHolder h;
    char message[2048], line[100];

    int file = open(CUSPATH, O_RDONLY | O_CREAT, 0644);
    int offset = lockCustomer(file, accountNumber, F_RDLCK, &c);
    if(offset == -1){
        close(file);
        sendInfo(connection_fd, "Account not found");
        return;
    }
    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

    sprintf(message, "\n--- Account Details ---\nAccount Number: %d\nName: %s\nBalance: %.2f\nStatus: %s\n",
            c.accountNumber, c.customerName, c.balance, c.activeStatus == 1 ? "Active" : "Deactivated");

    // If any holder belongs to this account, it is a joint account
    int holders = 0;
    file = open(JOINTPATH, O_RDONLY | O_CREAT, 0644);
    while(read(file, &h, sizeof(h)) == sizeof(h)){
        if(h.accountNumber == accountNumber){
            sprintf(line, "Holder ID: %d  Name: %s\n", h.holderID, h.name);
            strcat(message, line);
            holders++;
        }
    }
    close(file);

    strcat(message, holders > 0 ? "Account Type: Joint" : "Account Type: Normal");
    sendInfo(connection_fd, message);
}

// ======================= Apply loan =======================
void applyLoan(int connection_fd, int accountNumber){
    struct Counter ct;
    struct LoanDetails ld;

    askClient(connection_fd, "Enter Loan Amount: ");
    int amount = atoi(readBuffer);
    if(amount <= 0){
        sendInfo(connection_fd, "Amount must be greater than 0");
        return;
    }

    // Lock the whole counter file so two customers never get the same loan ID
    int file = open(COUNTERPATH, O_RDWR | O_CREAT, 0644);
    if(lockRecord(file, F_WRLCK, 0, 0) == -1){
        close(file);
        sendInfo(connection_fd, "Unable to apply for loan!");
        return;
    }
    ct.count = 0;
    read(file, &ct, sizeof(ct));
    ct.count++;
    int ok = saveRecord(file, 0, &ct, sizeof(ct));
    lockRecord(file, F_UNLCK, 0, 0);
    close(file);

    // If the loan is not saved after this, its ID is just skipped: no two loans share an ID
    if(ok){
        ld.empID = -1;            // no employee assigned yet
        ld.accountNumber = accountNumber;
        ld.loanAmount = amount;
        ld.status = 0;            // requested
        ld.loanID = ct.count;

        file = open(LOANPATH, O_WRONLY | O_APPEND | O_CREAT, 0644);
        ok = saveRecord(file, -1, &ld, sizeof(ld));
        close(file);
    }

    if(!ok){
        sendInfo(connection_fd, "Unable to apply for loan!");
        return;
    }
    printf("Applied loan of %d from acc no.: %d\n", amount, accountNumber);
    sendInfo(connection_fd, "Loan applied");
}

// ======================= Money Transfer =======================
void transferFunds(int connection_fd, int sourceAccount){
    struct Customer src, dst;
    char text[100];

    askClient(connection_fd, "Enter dest account number: ");
    int destAccount = atoi(readBuffer);
    askClient(connection_fd, "Enter amount: ");
    float amount = atof(readBuffer);

    if(amount <= 0){
        sendInfo(connection_fd, "Amount must be greater than 0");
        return;
    }
    if(destAccount == sourceAccount){
        sendInfo(connection_fd, "Cannot transfer to the same account");
        return;
    }

    int file = open(CUSPATH, O_RDWR);
    int srcOffset = findCustomer(file, sourceAccount, &src);
    int dstOffset = findCustomer(file, destAccount, &dst);
    if(srcOffset == -1 || dstOffset == -1 || dst.activeStatus != 1){
        close(file);
        sendInfo(connection_fd, "Account dosen't Exists or is not active!");
        return;
    }

    // Lock both accounts. We always lock the record that comes first in the file first.
    // So if A sends to B while B sends to A, both lock in the same order and
    // cannot wait for each other forever (no deadlock).
    if(srcOffset < dstOffset){
        srcOffset = lockCustomer(file, sourceAccount, F_WRLCK, &src);
        dstOffset = lockCustomer(file, destAccount, F_WRLCK, &dst);
    }
    else{
        dstOffset = lockCustomer(file, destAccount, F_WRLCK, &dst);
        srcOffset = lockCustomer(file, sourceAccount, F_WRLCK, &src);
    }

    // Check again under the locks: the accounts may have changed while we waited
    if(srcOffset == -1 || dstOffset == -1)
        strcpy(text, "Account dosen't Exists!");
    else if(src.activeStatus != 1 || dst.activeStatus != 1)
        strcpy(text, "Account is deactivated");
    else if(src.balance < amount)
        strcpy(text, "Insufficient funds in the source account.");
    else{
        // Both balances and both history lines are saved together through the journal,
        // so a crash can never take the money from one account without giving it to the other
        struct JournalEntry j;
        struct Customer oldSrc = src, oldDst = dst;
        startJournal(&j);
        src.balance -= amount;
        dst.balance += amount;
        journalWrite(&j, CUSFILE, file, srcOffset, &oldSrc, &src, sizeof(src));
        journalWrite(&j, CUSFILE, file, dstOffset, &oldDst, &dst, sizeof(dst));
        sprintf(text, "%.2f transferred into acc no %d", amount, destAccount);
        journalHistory(&j, sourceAccount, text);
        sprintf(text, "%.2f credited by acc no %d", amount, sourceAccount);
        journalHistory(&j, destAccount, text);

        if(commitJournal(&j)){
            printf("%.2f transferred into acc no %d from acc no %d\n", amount, destAccount, sourceAccount);
            sprintf(text, "Current Balance of %d is: %.2f", sourceAccount, src.balance);
        }
        else
            strcpy(text, "Transfer failed, please try again");
    }

    // Unlock before talking to the client, so others never wait for this client's network
    if(srcOffset != -1)
        lockRecord(file, F_UNLCK, srcOffset, sizeof(struct Customer));
    if(dstOffset != -1)
        lockRecord(file, F_UNLCK, dstOffset, sizeof(struct Customer));
    close(file);
    sendInfo(connection_fd, text);
}

// ======================= View Transaction History =======================
// Shows the 10 newest transactions, oldest of them first
void transactionHistory(int connection_fd, int accountNumber){
    char message[4000];
    struct trans_histroy th;
    int offsets[10];   // offsets of the last 10 lines of this account (used as a ring)
    int count = 0, i, offset = 0;

    bzero(message, sizeof(message));
    int file = open(HISTORYPATH, O_RDONLY | O_CREAT, 0644);
    while(read(file, &th, sizeof(th)) == sizeof(th)){
        if(th.acc_no == accountNumber){
            offsets[count % 10] = offset;
            count++;
        }
        offset += sizeof(th);
    }

    int first = count > 10 ? count - 10 : 0;
    for(i = first; i < count; i++){
        pread(file, &th, sizeof(th), offsets[i % 10]);
        strncat(message, th.hist, sizeof(message) - strlen(message) - 1);
    }
    close(file);

    if(count == 0)
        strcpy(message, "No transactions yet");
    sendInfo(connection_fd, message);
}

// ======================= Add Feedback =======================
void addFeedback(int connection_fd){
    struct FeedBack fb;

    askClient(connection_fd, "Enter Feedback:\n1. Good\n2. Bad\n3. Worse\n");
    int choice = atoi(readBuffer);

    bzero(&fb, sizeof(fb));
    if(choice == 1)
        strcpy(fb.feedback, "Good");
    else if(choice == 2)
        strcpy(fb.feedback, "Bad");
    else
        strcpy(fb.feedback, "Worse");

    int file = open(FEEDPATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
    int ok = saveRecord(file, -1, &fb, sizeof(fb));
    close(file);

    sendInfo(connection_fd, ok ? "Thank you for your feedback" : "Unable to save feedback");
}

// ======================= Change Password (normal customer) =======================
void changePassword(int connection_fd, int accountNumber){
    struct Customer c;
    char newPassword[256];

    askClient(connection_fd, "Enter password: ");
    copyInput(newPassword, sizeof(newPassword));

    int file = open(CUSPATH, O_RDWR);
    int offset = lockCustomer(file, accountNumber, F_WRLCK, &c);
    if(offset == -1){
        close(file);
        sendInfo(connection_fd, "Unable to change password");
        return;
    }

    strcpy(c.password, crypt(newPassword, HASHKEY));
    int ok = saveCustomer(file, offset, &c);

    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

    if(!ok){
        sendInfo(connection_fd, "Unable to change password");
        return;
    }
    printf("Customer %d changed password\n", accountNumber);
    sendInfo(connection_fd, "Password changed successfully");
}

// ======================= Change Password (joint holder) =======================
// Only changes the password of this holder, not of the other holder
void changeJointPassword(int connection_fd, int holderID){
    struct JointHolder h;
    char newPassword[256];

    askClient(connection_fd, "Enter password: ");
    copyInput(newPassword, sizeof(newPassword));

    int file = open(JOINTPATH, O_RDWR);
    int offset = findJointHolder(file, holderID, &h);
    if(offset == -1){
        close(file);
        sendInfo(connection_fd, "Unable to change password");
        return;
    }

    int ok = lockRecord(file, F_WRLCK, offset, sizeof(struct JointHolder)) != -1;
    if(ok){
        // Read again under the lock: the account may have been deleted meanwhile
        pread(file, &h, sizeof(h), offset);
        ok = h.holderID == holderID;
    }
    if(ok){
        strcpy(h.password, crypt(newPassword, HASHKEY));
        ok = saveRecord(file, offset, &h, sizeof(h));
    }
    lockRecord(file, F_UNLCK, offset, sizeof(struct JointHolder));
    close(file);

    if(!ok){
        sendInfo(connection_fd, "Unable to change password");
        return;
    }
    printf("Joint holder %d changed password\n", holderID);
    sendInfo(connection_fd, "Password changed successfully");
}
