void employeeMenu(int connection_fd);
int checkEmployee(int empID, char *password, int role);
void addCustomer(int connection_fd);
void askJointHolder(int connection_fd, int accountNumber, int holderNumber, struct JointHolder *h, int otherID);
void approveRejectLoan(int connection_fd, int empID);
void viewAssignedLoan(int connection_fd, int empID);
void changeEMPPassword(int connection_fd, int empID);

void employeeMenu(int connection_fd)
{
    int empID, accountNumber, choice;
    char password[256];

    askClient(connection_fd, "\nEnter Employee ID: ");
    empID = atoi(readBuffer);
    printf("Employee entered his ID: %d\n", empID);

    askClient(connection_fd, "Enter password: ");
    copyInput(password, sizeof(password));

    if(!checkEmployee(empID, password, 1))
    {
        sendInfo(connection_fd, "\nInvalid ID or Password");
        return;
    }
    // Employees and managers share one ID list, so both use the "emp" prefix
    if(!startSession("emp", empID))
    {
        sendInfo(connection_fd, "\nThis employee is already logged in from another session");
        return;
    }

    printf("%d logged In\n", empID);
    sendInfo(connection_fd, "\nLogin Successfully");

    while(1)
    {
        askClient(connection_fd, EMPMENU);
        choice = atoi(readBuffer);
        printf("Employee choose: %d\n", choice);

        switch(choice)
        {
            case 1:
                addCustomer(connection_fd);
                break;
            case 2:
                modifyCE(connection_fd, 1);
                break;
            case 3:
                approveRejectLoan(connection_fd, empID);
                break;
            case 4:
                viewAssignedLoan(connection_fd, empID);
                break;
            case 5:
                askClient(connection_fd, "Enter Account Number: ");
                accountNumber = atoi(readBuffer);
                transactionHistory(connection_fd, accountNumber);
                break;
            case 6:
                changeEMPPassword(connection_fd, empID);
                break;
            case 7:
                printf("Employee ID: %d Logged Out!\n", empID);
                logout(connection_fd);
                return;
            case 8:
                printf("Employee ID: %d Exited!\n", empID);
                exitClient(connection_fd);
                break;
            default:
                sendInfo(connection_fd, "Invalid Choice for Employee menu");
        }
    }
}

// Check ID, password and role (0 = manager, 1 = employee). Returns 1 if correct.
int checkEmployee(int empID, char *password, int role)
{
    struct Employee e;
    int file = open(EMPPATH, O_RDONLY | O_CREAT, 0644);
    int offset = findEmployee(file, empID, &e);
    close(file);

    return offset != -1 && e.role == role && strcmp(e.password, crypt(password, HASHKEY)) == 0;
}

// ================= Add Customer (normal or joint account) =================
// Used by employees and by the admin.
// We first ask for everything (also the joint holders) and then save it all in one step,
// so a client that disconnects halfway never leaves a joint account without holders.
void addCustomer(int connection_fd)
{
    struct Customer customer, temp;
    struct JointHolder holders[2], tempHolder;
    char text[100];

    bzero(&customer, sizeof(customer));

    askClient(connection_fd, "Enter Account Number: ");
    customer.accountNumber = atoi(readBuffer);
    if(customer.accountNumber <= 0)
    {
        sendInfo(connection_fd, "Invalid account number");
        return;
    }

    askClient(connection_fd, "Enter Account Type (1. Normal  2. Joint): ");
    int type = atoi(readBuffer);
    if(type != 1 && type != 2)
    {
        sendInfo(connection_fd, "Invalid account type");
        return;
    }

    askClient(connection_fd, "Enter Name: ");
    copyInput(customer.customerName, sizeof(customer.customerName));

    // A joint account keeps an empty password here: its holders log in with their own IDs
    if(type == 1)
    {
        askClient(connection_fd, "Enter password: ");
        strcpy(customer.password, crypt(readBuffer, HASHKEY));
    }

    askClient(connection_fd, "Enter Opening Balance: ");
    customer.balance = atof(readBuffer);
    if(customer.balance < 0)
    {
        sendInfo(connection_fd, "Opening balance cannot be negative");
        return;
    }
    customer.activeStatus = 1;

    if(type == 2)
    {
        askJointHolder(connection_fd, customer.accountNumber, 1, &holders[0], -1);
        askJointHolder(connection_fd, customer.accountNumber, 2, &holders[1], holders[0].holderID);
    }

    // Lock both files completely, so two staff members cannot add the same
    // account number or the same holder ID at the same time.
    // Always customers.txt first, then jointHolders.txt (same order as deleteAccount).
    int file = open(CUSPATH, O_RDWR | O_CREAT, 0644);
    int jointFile = open(JOINTPATH, O_RDWR | O_CREAT, 0644);
    int locked = lockRecord(file, F_WRLCK, 0, 0) != -1 && lockRecord(jointFile, F_WRLCK, 0, 0) != -1;

    if(!locked)
        strcpy(text, "Unable to add customer, please try again");
    else if(findCustomer(file, customer.accountNumber, &temp) != -1)
        strcpy(text, "Account number already exists");
    else if(type == 2 && (findJointHolder(jointFile, holders[0].holderID, &tempHolder) != -1 ||
                          findJointHolder(jointFile, holders[1].holderID, &tempHolder) != -1))
        strcpy(text, "A holder ID was taken meanwhile, please try again");
    else
    {
        // The account, its holders and the opening balance line are saved together
        struct JournalEntry j;
        startJournal(&j);
        journalWrite(&j, CUSFILE, file, lseek(file, 0, SEEK_END), NULL, &customer, sizeof(customer));
        if(type == 2)
        {
            int end = lseek(jointFile, 0, SEEK_END);
            journalWrite(&j, JOINTFILE, jointFile, end, NULL, &holders[0], sizeof(struct JointHolder));
            journalWrite(&j, JOINTFILE, jointFile, end + sizeof(struct JointHolder), NULL, &holders[1], sizeof(struct JointHolder));
        }
        sprintf(text, "%.2f Opening Balance", customer.balance);
        journalHistory(&j, customer.accountNumber, text);

        if(commitJournal(&j))
        {
            printf("Customer added whose account number is: %d\n", customer.accountNumber);
            strcpy(text, "Customer added successfully!");
        }
        else
            strcpy(text, "Unable to add customer, please try again");
    }

    lockRecord(file, F_UNLCK, 0, 0);
    lockRecord(jointFile, F_UNLCK, 0, 0);
    close(file);
    close(jointFile);
    sendInfo(connection_fd, text);
}

// Ask for the details of one holder of a joint account (they are saved by addCustomer).
// otherID is the ID of the other holder, which this one must not reuse (-1 = none).
void askJointHolder(int connection_fd, int accountNumber, int holderNumber, struct JointHolder *h, int otherID)
{
    struct JointHolder temp;
    char prompt[100];

    bzero(h, sizeof(*h));
    h->accountNumber = accountNumber;

    // Keep asking until we get a holder ID that is not used yet.
    // addCustomer checks it again under the lock before saving.
    while(1)
    {
        sprintf(prompt, "Enter Holder %d ID: ", holderNumber);
        askClient(connection_fd, prompt);
        h->holderID = atoi(readBuffer);

        int file = open(JOINTPATH, O_RDONLY | O_CREAT, 0644);
        int exists = findJointHolder(file, h->holderID, &temp) != -1;
        close(file);

        if(h->holderID > 0 && h->holderID != otherID && !exists)
            break;
        sendInfo(connection_fd, "Invalid or already used holder ID, try again");
    }

    sprintf(prompt, "Enter Holder %d Name: ", holderNumber);
    askClient(connection_fd, prompt);
    copyInput(h->name, sizeof(h->name));

    askClient(connection_fd, "Enter password: ");
    strcpy(h->password, crypt(readBuffer, HASHKEY));
}

// ================= Approve/Reject Loan =================
void approveRejectLoan(int connection_fd, int empID)
{
    struct LoanDetails ld;
    struct Customer cs;
    char text[100];

    askClient(connection_fd, "Enter Loan ID: ");
    int lID = atoi(readBuffer);

    int file = open(LOANPATH, O_RDWR | O_CREAT, 0644);
    int offset = findLoan(file, lID, &ld);
    if(offset == -1)
    {
        close(file);
        sendInfo(connection_fd, "Invalid loan ID");
        return;
    }

    // Only a pending loan that is assigned to this employee can be processed (no double approval).
    // Checked here without a lock to answer quickly, and checked again below under the lock.
    if(ld.status != 1 || ld.empID != empID)
    {
        close(file);
        sendInfo(connection_fd, "This loan is not assigned to you or is already approved/rejected");
        return;
    }

    // Ask before locking: a record must never stay locked while we wait for the user
    askClient(connection_fd, "Enter 1 to Approve Loan\nEnter 2 to Reject Loan: ");
    int choice = atoi(readBuffer);
    if(choice != 1 && choice != 2)
    {
        close(file);
        sendInfo(connection_fd, "Invalid Choice");
        return;
    }

    // Lock the loan record, then read it again in case it changed meanwhile
    if(lockRecord(file, F_WRLCK, offset, sizeof(struct LoanDetails)) == -1)
    {
        close(file);
        sendInfo(connection_fd, "Unable to process the loan, please try again");
        return;
    }
    pread(file, &ld, sizeof(ld), offset);
    struct LoanDetails oldLoan = ld;

    if(ld.status != 1 || ld.empID != empID)
    {
        strcpy(text, "This loan was changed meanwhile and cannot be processed");
    }
    else if(choice == 2)
    {
        ld.status = 3;   // rejected
        strcpy(text, saveRecord(file, offset, &ld, sizeof(ld)) ? "Loan rejected" : "Unable to process the loan, please try again");
    }
    else
    {
        struct JournalEntry j;
        startJournal(&j);

        // Lock order is always: loan record first, then customer record
        int cusFile = open(CUSPATH, O_RDWR);
        int cusOffset = lockCustomer(cusFile, ld.accountNumber, F_WRLCK, &cs);

        if(cusOffset == -1 || cs.activeStatus != 1)
        {
            ld.status = 3;   // rejected
            strcpy(text, "Account is deleted or deactivated, so the loan is rejected");
        }
        else
        {
            struct Customer oldCs = cs;
            char hist[100];
            cs.balance += ld.loanAmount;
            ld.status = 2;   // approved
            journalWrite(&j, CUSFILE, cusFile, cusOffset, &oldCs, &cs, sizeof(cs));
            sprintf(hist, "%d credited by loan id %d", ld.loanAmount, lID);
            journalHistory(&j, ld.accountNumber, hist);
            strcpy(text, "Loan Approved");
        }

        // The new balance and the new loan status are saved together, so after a crash
        // the loan can never be "pending" again with the money already paid out
        journalWrite(&j, LOANFILE, file, offset, &oldLoan, &ld, sizeof(ld));
        if(!commitJournal(&j))
            strcpy(text, "Unable to process the loan, please try again");

        if(cusOffset != -1)
            lockRecord(cusFile, F_UNLCK, cusOffset, sizeof(struct Customer));
        close(cusFile);
    }

    lockRecord(file, F_UNLCK, offset, sizeof(struct LoanDetails));
    close(file);

    printf("Employee %d processed loan %d: %s\n", empID, lID, text);
    sendInfo(connection_fd, text);
}

// ================= View Assigned Loan =================
void viewAssignedLoan(int connection_fd, int empID)
{
    struct LoanDetails ld;
    char message[4000], line[200];
    int count = 0;

    bzero(message, sizeof(message));
    int file = open(LOANPATH, O_RDONLY | O_CREAT, 0644);
    while(read(file, &ld, sizeof(ld)) == sizeof(ld))
    {
        if(ld.empID == empID && ld.status == 1 && strlen(message) < sizeof(message) - sizeof(line))
        {
            sprintf(line, "Loan ID: %d  Account Number: %d  Loan Amount: %d\n", ld.loanID, ld.accountNumber, ld.loanAmount);
            strcat(message, line);
            count++;
        }
    }
    close(file);

    if(count == 0)
        strcpy(message, "No pending loans assigned to you");
    sendInfo(connection_fd, message);
}

// ================= Change Password (employee and manager) =================
void changeEMPPassword(int connection_fd, int empID)
{
    struct Employee emp;
    char newPassword[256];

    askClient(connection_fd, "Enter password: ");
    copyInput(newPassword, sizeof(newPassword));

    int file = open(EMPPATH, O_RDWR);
    int offset = findEmployee(file, empID, &emp);
    if(offset == -1)
    {
        close(file);
        sendInfo(connection_fd, "Unable to change password");
        return;
    }

    int ok = lockRecord(file, F_WRLCK, offset, sizeof(struct Employee)) != -1;
    if(ok)
    {
        pread(file, &emp, sizeof(emp), offset);
        strcpy(emp.password, crypt(newPassword, HASHKEY));
        ok = saveRecord(file, offset, &emp, sizeof(emp));
    }
    lockRecord(file, F_UNLCK, offset, sizeof(struct Employee));
    close(file);

    if(!ok)
    {
        sendInfo(connection_fd, "Unable to change password");
        return;
    }
    printf("Employee %d changed password\n", empID);
    sendInfo(connection_fd, "Password changed successfully");
}
