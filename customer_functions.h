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

    c.balance += amount;
    saveCustomer(file, offset, &c);

    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

    sprintf(text, "%.2f deposited", amount);
    addHistory(accountNumber, text);
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
    if(c.balance < amount){
        lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
        close(file);
        printf("Insufficient balance in account: %d\n", accountNumber);
        sendInfo(connection_fd, "Insufficient funds!");
        return;
    }

    c.balance -= amount;
    saveCustomer(file, offset, &c);

    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

    sprintf(text, "%.2f withdraw", amount);
    addHistory(accountNumber, text);
    printf("New balance of %d account number: %.2f\n", accountNumber, c.balance);
    sendInfo(connection_fd, "Withdrawal successful!");
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
    lockRecord(file, F_WRLCK, 0, 0);
    ct.count = 0;
    read(file, &ct, sizeof(ct));
    ct.count++;
    lseek(file, 0, SEEK_SET);
    write(file, &ct, sizeof(ct));
    lockRecord(file, F_UNLCK, 0, 0);
    close(file);

    ld.empID = -1;            // no employee assigned yet
    ld.accountNumber = accountNumber;
    ld.loanAmount = amount;
    ld.status = 0;            // requested
    ld.loanID = ct.count;

    file = open(LOANPATH, O_WRONLY | O_APPEND | O_CREAT, 0644);
    int response = write(file, &ld, sizeof(ld));
    close(file);

    printf("Applied loan of %d from acc no.: %d\n", amount, accountNumber);
    if(response <= 0)
        sendInfo(connection_fd, "Unable to apply for loan!");
    else
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

    int ok = 1;
    if(srcOffset == -1 || dstOffset == -1){
        sendInfo(connection_fd, "Account dosen't Exists!");
        ok = 0;
    }
    else if(src.balance < amount){
        sendInfo(connection_fd, "Insufficient funds in the source account.");
        ok = 0;
    }
    else{
        src.balance -= amount;
        dst.balance += amount;
        saveCustomer(file, srcOffset, &src);
        saveCustomer(file, dstOffset, &dst);
    }

    if(srcOffset != -1)
        lockRecord(file, F_UNLCK, srcOffset, sizeof(struct Customer));
    if(dstOffset != -1)
        lockRecord(file, F_UNLCK, dstOffset, sizeof(struct Customer));
    close(file);

    if(!ok)
        return;

    sprintf(text, "%.2f transferred into acc no %d", amount, destAccount);
    addHistory(sourceAccount, text);
    sprintf(text, "%.2f credited by acc no %d", amount, sourceAccount);
    addHistory(destAccount, text);

    printf("%.2f transferred into acc no %d from acc no %d\n", amount, destAccount, sourceAccount);
    sprintf(text, "Current Balance of %d is: %.2f", sourceAccount, src.balance);
    sendInfo(connection_fd, text);
}

// ======================= View Transaction History =======================
void transactionHistory(int connection_fd, int accountNumber){
    char message[4000];
    struct trans_histroy th;
    int count = 0;

    bzero(message, sizeof(message));
    int file = open(HISTORYPATH, O_RDONLY | O_CREAT, 0644);
    while(read(file, &th, sizeof(th)) == sizeof(th) && count < 10){
        if(th.acc_no == accountNumber){
            strncat(message, th.hist, sizeof(message) - strlen(message) - 1);
            count++;
        }
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
    write(file, &fb, sizeof(fb));
    close(file);

    sendInfo(connection_fd, "Thank you for your feedback");
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
    saveCustomer(file, offset, &c);

    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

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

    lockRecord(file, F_WRLCK, offset, sizeof(struct JointHolder));
    lseek(file, offset, SEEK_SET);
    read(file, &h, sizeof(h));
    strcpy(h.password, crypt(newPassword, HASHKEY));
    lseek(file, offset, SEEK_SET);
    write(file, &h, sizeof(h));
    lockRecord(file, F_UNLCK, offset, sizeof(struct JointHolder));
    close(file);

    printf("Joint holder %d changed password\n", holderID);
    sendInfo(connection_fd, "Password changed successfully");
}
