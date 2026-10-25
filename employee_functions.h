void employeeMenu(int connection_fd);
int checkEmployee(int empID, char *password, int role);
void addCustomer(int connection_fd);
void addJointHolder(int connection_fd, int accountNumber, int holderNumber);
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
// Used by employees and by the admin
void addCustomer(int connection_fd)
{
    struct Customer customer, temp;
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

    // Lock the whole file so two staff members cannot add the same account number at the same time
    int file = open(CUSPATH, O_RDWR | O_CREAT, 0644);
    lockRecord(file, F_WRLCK, 0, 0);
    if(findCustomer(file, customer.accountNumber, &temp) != -1)
    {
        lockRecord(file, F_UNLCK, 0, 0);
        close(file);
        sendInfo(connection_fd, "Account number already exists");
        return;
    }
    lseek(file, 0, SEEK_END);
    write(file, &customer, sizeof(customer));
    lockRecord(file, F_UNLCK, 0, 0);
    close(file);

    sprintf(text, "%.2f Opening Balance", customer.balance);
    addHistory(customer.accountNumber, text);

    if(type == 2)
    {
        addJointHolder(connection_fd, customer.accountNumber, 1);
        addJointHolder(connection_fd, customer.accountNumber, 2);
    }

    printf("Customer added whose account number is: %d\n", customer.accountNumber);
    sendInfo(connection_fd, "Customer added successfully!");
}

// Ask for the details of one holder of a joint account and save them
void addJointHolder(int connection_fd, int accountNumber, int holderNumber)
{
    struct JointHolder h, temp;
    char prompt[100];

    bzero(&h, sizeof(h));
    h.accountNumber = accountNumber;

    // Keep asking until we get a holder ID that is not used yet
    while(1)
    {
        sprintf(prompt, "Enter Holder %d ID: ", holderNumber);
        askClient(connection_fd, prompt);
        h.holderID = atoi(readBuffer);

        int file = open(JOINTPATH, O_RDONLY | O_CREAT, 0644);
        int exists = findJointHolder(file, h.holderID, &temp) != -1;
        close(file);

        if(h.holderID > 0 && !exists)
            break;
        sendInfo(connection_fd, "Invalid or already used holder ID, try again");
    }

    sprintf(prompt, "Enter Holder %d Name: ", holderNumber);
    askClient(connection_fd, prompt);
    copyInput(h.name, sizeof(h.name));

    askClient(connection_fd, "Enter password: ");
    strcpy(h.password, crypt(readBuffer, HASHKEY));

    int file = open(JOINTPATH, O_WRONLY | O_APPEND | O_CREAT, 0644);
    write(file, &h, sizeof(h));
    close(file);
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

    // Lock the loan record, then read it again in case it changed meanwhile
    lockRecord(file, F_WRLCK, offset, sizeof(struct LoanDetails));
    lseek(file, offset, SEEK_SET);
    read(file, &ld, sizeof(ld));

    // Only a pending loan that is assigned to this employee can be processed (no double approval)
    if(ld.status != 1 || ld.empID != empID)
    {
        lockRecord(file, F_UNLCK, offset, sizeof(struct LoanDetails));
        close(file);
        sendInfo(connection_fd, "This loan is not assigned to you or is already approved/rejected");
        return;
    }

    askClient(connection_fd, "Enter 1 to Approve Loan\nEnter 2 to Reject Loan: ");
    int choice = atoi(readBuffer);

    if(choice == 1)
    {
        int cusFile = open(CUSPATH, O_RDWR);
        int cusOffset = lockCustomer(cusFile, ld.accountNumber, F_WRLCK, &cs);

        if(cusOffset == -1 || cs.activeStatus != 1)
        {
            ld.status = 3;   // rejected
            strcpy(text, "Account is deleted or deactivated, so the loan is rejected");
        }
        else
        {
            cs.balance += ld.loanAmount;
            saveCustomer(cusFile, cusOffset, &cs);
            ld.status = 2;   // approved
            strcpy(text, "Loan Approved");
        }

        if(cusOffset != -1)
            lockRecord(cusFile, F_UNLCK, cusOffset, sizeof(struct Customer));
        close(cusFile);

        if(ld.status == 2)
        {
            char hist[100];
            sprintf(hist, "%d credited by loan id %d", ld.loanAmount, lID);
            addHistory(ld.accountNumber, hist);
        }
    }
    else if(choice == 2)
    {
        ld.status = 3;   // rejected
        strcpy(text, "Loan rejected");
    }
    else
    {
        strcpy(text, "Invalid Choice");
    }

    // Save the new loan status (only changed for choice 1 or 2)
    lseek(file, offset, SEEK_SET);
    write(file, &ld, sizeof(ld));

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

    lockRecord(file, F_WRLCK, offset, sizeof(struct Employee));
    lseek(file, offset, SEEK_SET);
    read(file, &emp, sizeof(emp));
    strcpy(emp.password, crypt(newPassword, HASHKEY));
    lseek(file, offset, SEEK_SET);
    write(file, &emp, sizeof(emp));
    lockRecord(file, F_UNLCK, offset, sizeof(struct Employee));
    close(file);

    printf("Employee %d changed password\n", empID);
    sendInfo(connection_fd, "Password changed successfully");
}
