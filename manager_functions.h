void managerMenu(int connection_fd);
void changeStatus(int connection_fd);
void assignLoanApplication(int connection_fd);
void readFeedBack(int connection_fd);

void managerMenu(int connection_fd)
{
    int mngID, choice;
    char password[256];

    askClient(connection_fd, "\nEnter Manager ID: ");
    mngID = atoi(readBuffer);

    askClient(connection_fd, "Enter password: ");
    copyInput(password, sizeof(password));

    if(!checkEmployee(mngID, password, 0))
    {
        sendInfo(connection_fd, "\nInvalid ID or Password");
        return;
    }
    if(!startSession("emp", mngID))
    {
        sendInfo(connection_fd, "\nThis manager is already logged in from another session");
        return;
    }

    printf("Manager whose ID: %d logged In\n", mngID);
    sendInfo(connection_fd, "Login Successfully\n");

    while(1)
    {
        askClient(connection_fd, MNGMENU);
        choice = atoi(readBuffer);
        printf("Manager entered: %d\n", choice);

        switch(choice)
        {
            case 1:
                // Activate/Deactivate Customer Accounts
                changeStatus(connection_fd);
                break;
            case 2:
                // Assign Loan Application Processes to Employees
                assignLoanApplication(connection_fd);
                break;
            case 3:
                // Review Customer Feedback
                readFeedBack(connection_fd);
                break;
            case 4:
                changeEMPPassword(connection_fd, mngID);
                break;
            case 5:
                printf("Manager Logged out!\n");
                logout(connection_fd);
                return;
            case 6:
                printf("Manager exited\n");
                exitClient(connection_fd);
                break;
            default:
                sendInfo(connection_fd, "Invalid Choice for Manager menu");
        }
    }
}

// ==================== Activate/Deactivate Customer Accounts ====================
void changeStatus(int connection_fd)
{
    struct Customer cs;

    askClient(connection_fd, "Enter Account Number: ");
    int accNo = atoi(readBuffer);

    askClient(connection_fd, "Enter 1 to Deactivate\nEnter 2 to activate\nEnter your choice: ");
    int choice = atoi(readBuffer);
    if(choice != 1 && choice != 2)
    {
        sendInfo(connection_fd, "Invalid choice");
        return;
    }

    int file = open(CUSPATH, O_RDWR);
    int offset = lockCustomer(file, accNo, F_WRLCK, &cs);
    if(offset == -1)
    {
        close(file);
        printf("Manager entered invalid account number: %d\n", accNo);
        sendInfo(connection_fd, "Invalid account number");
        return;
    }

    cs.activeStatus = (choice == 1) ? 0 : 1;
    int ok = saveCustomer(file, offset, &cs);
    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

    if(!ok)
    {
        sendInfo(connection_fd, "Unable to change status");
        return;
    }
    printf("Manager %s customer %d\n", choice == 1 ? "deactivated" : "activated", accNo);
    sendInfo(connection_fd, "Status Changed Successfully");
}

// ==================== Read Feedback =========================
void readFeedBack(int connection_fd)
{
    struct FeedBack fb;
    char message[4000];

    bzero(message, sizeof(message));
    int file = open(FEEDPATH, O_RDONLY | O_CREAT, 0644);
    while(read(file, &fb, sizeof(fb)) == sizeof(fb) && strlen(message) < sizeof(message) - 20)
    {
        strcat(message, fb.feedback);
        strcat(message, "\n");
    }
    close(file);

    if(strlen(message) == 0)
        strcpy(message, "No feedback yet");
    printf("Manager reading customers feedback\n");
    sendInfo(connection_fd, message);
}

// ==================== Assign Loan Application Processes to Employees ====================
void assignLoanApplication(int connection_fd)
{
    struct LoanDetails ld;
    struct Employee emp;
    char message[4000], line[200];

    // Show all loans that are not assigned yet
    bzero(message, sizeof(message));
    strcpy(message, "Unassigned loans:\n");
    int file = open(LOANPATH, O_RDWR | O_CREAT, 0644);
    while(read(file, &ld, sizeof(ld)) == sizeof(ld))
    {
        if(ld.status == 0 && strlen(message) < sizeof(message) - sizeof(line))
        {
            sprintf(line, "Loan ID: %d  Account Number: %d  Loan Amount: %d\n", ld.loanID, ld.accountNumber, ld.loanAmount);
            strcat(message, line);
        }
    }
    sendInfo(connection_fd, message);

    askClient(connection_fd, "Enter Loan ID: ");
    int lID = atoi(readBuffer);
    askClient(connection_fd, "Enter Employee ID: ");
    int eID = atoi(readBuffer);

    // The employee must exist and must not be a manager
    int empFile = open(EMPPATH, O_RDONLY | O_CREAT, 0644);
    int empOffset = findEmployee(empFile, eID, &emp);
    close(empFile);
    if(empOffset == -1 || emp.role != 1)
    {
        close(file);
        sendInfo(connection_fd, "Invalid Employee ID");
        return;
    }

    int offset = findLoan(file, lID, &ld);
    if(offset == -1)
    {
        close(file);
        sendInfo(connection_fd, "Invalid loan ID");
        return;
    }

    // Lock the loan record and read it again in case it changed meanwhile
    if(lockRecord(file, F_WRLCK, offset, sizeof(struct LoanDetails)) == -1)
    {
        close(file);
        sendInfo(connection_fd, "Unable to assign the loan, please try again");
        return;
    }
    pread(file, &ld, sizeof(ld), offset);

    if(ld.status != 0)
    {
        sprintf(line, "Loan %d is already assigned or processed", lID);
    }
    else
    {
        ld.empID = eID;
        ld.status = 1; // Pending
        if(saveRecord(file, offset, &ld, sizeof(ld)))
        {
            sprintf(line, "Loan %d assigned to employee %d", lID, eID);
            printf("Manager assigned employee %d loan application %d\n", eID, lID);
        }
        else
            sprintf(line, "Unable to assign loan %d", lID);
    }

    lockRecord(file, F_UNLCK, offset, sizeof(struct LoanDetails));
    close(file);
    sendInfo(connection_fd, line);
}
