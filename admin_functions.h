#define ADMINNAME "admin"
#define PASSWORD "admin@123"

void addEmployee(int connection_fd);
void deleteAccount(int connection_fd);
void modifyCE(int connection_fd, int modifyChoice);
void manageRole(int connection_fd);

void adminMenu(int connection_fd)
{
    int choice;

    askClient(connection_fd, "Enter password: ");
    if(strcmp(PASSWORD, readBuffer) != 0)
    {
        sendInfo(connection_fd, "\nInvalid credential");
        return;
    }
    // There is only one admin, so the ID is always 0
    if(!startSession("admin", 0))
    {
        sendInfo(connection_fd, "\nAdmin is already logged in from another session");
        return;
    }
    sendInfo(connection_fd, "\nLogin Successfully");

    while(1)
    {
        askClient(connection_fd, ADMINMENU);
        choice = atoi(readBuffer);
        printf("Admin entered: %d\n", choice);

        switch (choice) {
            case 1:
                addEmployee(connection_fd);
                break;
            case 2:
                addCustomer(connection_fd);
                break;
            case 3:
                deleteAccount(connection_fd);
                break;
            case 4:
                askClient(connection_fd, "Enter 1 to Modify Customer\nEnter 2 to Modify Employee: ");
                modifyCE(connection_fd, atoi(readBuffer));
                break;
            case 5:
                // Search account: show all its details
                askClient(connection_fd, "Enter Account Number: ");
                viewDetails(connection_fd, atoi(readBuffer));
                break;
            case 6:
                manageRole(connection_fd);
                break;
            case 7:
                logout(connection_fd);
                return;
            case 8:
                exitClient(connection_fd);
                break;
            default:
                sendInfo(connection_fd, "Invalid choice! Please try again.");
        }
    }
}

// ================ Add New Bank Employee ================
void addEmployee(int connection_fd)
{
    struct Employee emp, temp;

    bzero(&emp, sizeof(emp));

    askClient(connection_fd, "Enter Employee ID: ");
    emp.empID = atoi(readBuffer);
    if(emp.empID <= 0)
    {
        sendInfo(connection_fd, "Invalid Employee ID");
        return;
    }

    askClient(connection_fd, "Enter FirstName: ");
    copyInput(emp.firstName, sizeof(emp.firstName));

    askClient(connection_fd, "Enter LastName: ");
    copyInput(emp.lastName, sizeof(emp.lastName));

    askClient(connection_fd, "Enter password: ");
    strcpy(emp.password, crypt(readBuffer, HASHKEY));

    emp.role = 1;

    // Lock the whole file while we check for a duplicate ID and add the new employee
    int file = open(EMPPATH, O_RDWR | O_CREAT, 0644);
    lockRecord(file, F_WRLCK, 0, 0);
    if(findEmployee(file, emp.empID, &temp) != -1)
    {
        lockRecord(file, F_UNLCK, 0, 0);
        close(file);
        sendInfo(connection_fd, "Employee ID already exists");
        return;
    }
    lseek(file, 0, SEEK_END);
    write(file, &emp, sizeof(emp));
    lockRecord(file, F_UNLCK, 0, 0);
    close(file);

    printf("Admin added employee %d\n", emp.empID);
    sendInfo(connection_fd, "Employee successfully added\n");
}

// ================ Delete Customer Account ================
// We don't remove the record and shift the rest of the file. Other clients may be
// working with record offsets at this moment, so we only mark the slot as deleted
// (account number -1, empty password). Searches will never find it again.
void deleteAccount(int connection_fd)
{
    struct Customer c;
    struct JointHolder h;

    askClient(connection_fd, "Enter Account Number to delete: ");
    int accNo = atoi(readBuffer);

    int file = open(CUSPATH, O_RDWR | O_CREAT, 0644);
    int offset = lockCustomer(file, accNo, F_WRLCK, &c);
    if(offset == -1)
    {
        close(file);
        sendInfo(connection_fd, "Invalid Account Number");
        return;
    }

    bzero(&c, sizeof(c));
    c.accountNumber = -1;
    saveCustomer(file, offset, &c);
    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);

    // Also delete the holders if it was a joint account
    file = open(JOINTPATH, O_RDWR | O_CREAT, 0644);
    int hOffset = 0;
    while(read(file, &h, sizeof(h)) == sizeof(h))
    {
        if(h.accountNumber == accNo)
        {
            bzero(&h, sizeof(h));
            h.holderID = -1;
            h.accountNumber = -1;
            lockRecord(file, F_WRLCK, hOffset, sizeof(h));
            lseek(file, hOffset, SEEK_SET);
            write(file, &h, sizeof(h));
            lockRecord(file, F_UNLCK, hOffset, sizeof(h));
        }
        hOffset += sizeof(h);
        lseek(file, hOffset, SEEK_SET);
    }
    close(file);

    printf("Admin deleted account %d\n", accNo);
    sendInfo(connection_fd, "Account deleted successfully");
}

// ===================== Modify Customer / Employee ==================
// modifyChoice: 1 = customer (also used by employees), 2 = employee
void modifyCE(int connection_fd, int modifyChoice)
{
    char newName[20];

    if(modifyChoice == 1)
    {
        struct Customer c;

        askClient(connection_fd, "Enter Account Number: ");
        int accNo = atoi(readBuffer);
        askClient(connection_fd, "Enter New Name: ");
        copyInput(newName, sizeof(newName));

        int file = open(CUSPATH, O_RDWR | O_CREAT, 0644);
        int offset = lockCustomer(file, accNo, F_WRLCK, &c);
        if(offset == -1)
        {
            close(file);
            sendInfo(connection_fd, "Invalid Account Number\n");
            return;
        }

        strcpy(c.customerName, newName);
        saveCustomer(file, offset, &c);
        lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
        close(file);

        printf("Name of account %d changed to %s\n", accNo, newName);
        sendInfo(connection_fd, "Name changed successfully");
    }
    else if(modifyChoice == 2)
    {
        struct Employee emp;

        askClient(connection_fd, "Enter Employee ID: ");
        int id = atoi(readBuffer);
        askClient(connection_fd, "Enter New Name: ");
        copyInput(newName, sizeof(newName));

        int file = open(EMPPATH, O_RDWR | O_CREAT, 0644);
        int offset = findEmployee(file, id, &emp);
        if(offset == -1)
        {
            close(file);
            sendInfo(connection_fd, "Invalid Employee ID");
            return;
        }

        lockRecord(file, F_WRLCK, offset, sizeof(struct Employee));
        lseek(file, offset, SEEK_SET);
        read(file, &emp, sizeof(emp));
        strcpy(emp.firstName, newName);
        lseek(file, offset, SEEK_SET);
        write(file, &emp, sizeof(emp));
        lockRecord(file, F_UNLCK, offset, sizeof(struct Employee));
        close(file);

        printf("Admin changed name of employee: %d\n", id);
        sendInfo(connection_fd, "Name changed successfully");
    }
    else
    {
        sendInfo(connection_fd, "Invalid choice");
    }
}

// ===================== Manage Role ==================
void manageRole(int connection_fd)
{
    struct Employee emp;

    askClient(connection_fd, "Enter ID: ");
    int id = atoi(readBuffer);
    askClient(connection_fd, "Enter 1 to make manager\nEnter 2 to make employee: ");
    int choice = atoi(readBuffer);
    if(choice != 1 && choice != 2)
    {
        sendInfo(connection_fd, "Invalid choice");
        return;
    }

    int file = open(EMPPATH, O_RDWR | O_CREAT, 0644);
    int offset = findEmployee(file, id, &emp);
    if(offset == -1)
    {
        close(file);
        sendInfo(connection_fd, "Invalid ID");
        return;
    }

    lockRecord(file, F_WRLCK, offset, sizeof(struct Employee));
    lseek(file, offset, SEEK_SET);
    read(file, &emp, sizeof(emp));
    emp.role = (choice == 1) ? 0 : 1;
    lseek(file, offset, SEEK_SET);
    write(file, &emp, sizeof(emp));
    lockRecord(file, F_UNLCK, offset, sizeof(struct Employee));
    close(file);

    printf("Admin made %d %s\n", id, choice == 1 ? "manager" : "employee");
    sendInfo(connection_fd, "Role changed successfully");
}
