#define ADMINNAME "admin"
#define PASSWORD "admin@123"

void addEmployee(int connection_fd);
void deleteAccount(int connection_fd);
void modifyCE(int connection_fd, int modifyChoice);
void manageRole(int connection_fd);
void viewEmployee(int connection_fd);

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
                viewEmployee(connection_fd);
                break;
            case 8:
                logout(connection_fd);
                return;
            case 9:
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
    char *message = "Employee successfully added\n";
    int file = open(EMPPATH, O_RDWR | O_CREAT, 0644);
    if(lockRecord(file, F_WRLCK, 0, 0) == -1)
        message = "Unable to add employee";
    else if(findEmployee(file, emp.empID, &temp) != -1)
        message = "Employee ID already exists";
    else if(!saveRecord(file, -1, &emp, sizeof(emp)))
        message = "Unable to add employee";
    else
        printf("Admin added employee %d\n", emp.empID);
    lockRecord(file, F_UNLCK, 0, 0);
    close(file);

    sendInfo(connection_fd, message);
}

// ================ View Employee Details ================
void viewEmployee(int connection_fd)
{
    struct Employee emp;
    char message[200];

    askClient(connection_fd, "Enter Employee ID: ");
    int id = atoi(readBuffer);

    int file = open(EMPPATH, O_RDONLY | O_CREAT, 0644);
    int offset = findEmployee(file, id, &emp);
    if(offset == -1)
    {
        close(file);
        sendInfo(connection_fd, "Employee not found");
        return;
    }

    // Read lock while reading, so we never see a half-written record
    lockRecord(file, F_RDLCK, offset, sizeof(struct Employee));
    lseek(file, offset, SEEK_SET);
    read(file, &emp, sizeof(emp));
    lockRecord(file, F_UNLCK, offset, sizeof(struct Employee));
    close(file);

    sprintf(message, "\n--- Employee Details ---\nEmployee ID: %d\nName: %s %s\nRole: %s",
            emp.empID, emp.firstName, emp.lastName, emp.role == 0 ? "Manager" : "Employee");
    sendInfo(connection_fd, message);
}

// ================ Delete Customer Account ================
// We don't remove the record and shift the rest of the file. Other clients may be
// working with record offsets at this moment, so we only mark the slot as deleted
// (account number -1, empty password). Searches will never find it again.
// The account and its joint holders are deleted together through the journal.
void deleteAccount(int connection_fd)
{
    struct Customer c, deleted;
    struct JointHolder h, deletedHolder;
    struct JournalEntry j;
    char *message = "Account deleted successfully";

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

    startJournal(&j);
    bzero(&deleted, sizeof(deleted));
    deleted.accountNumber = -1;
    journalWrite(&j, CUSFILE, file, offset, &c, &deleted, sizeof(deleted));

    // Lock the whole holder file (after customers.txt, same order as addCustomer)
    // and also delete the holders if it was a joint account
    int jointFile = open(JOINTPATH, O_RDWR | O_CREAT, 0644);
    int ok = lockRecord(jointFile, F_WRLCK, 0, 0) != -1;
    int hOffset = 0;
    bzero(&deletedHolder, sizeof(deletedHolder));
    deletedHolder.holderID = -1;
    deletedHolder.accountNumber = -1;
    while(ok && read(jointFile, &h, sizeof(h)) == sizeof(h))
    {
        if(h.accountNumber == accNo && j.writes < MAXWRITES)
            journalWrite(&j, JOINTFILE, jointFile, hOffset, &h, &deletedHolder, sizeof(h));
        hOffset += sizeof(h);
    }

    if(!ok || !commitJournal(&j))
        message = "Unable to delete the account, please try again";
    else
        printf("Admin deleted account %d\n", accNo);

    lockRecord(jointFile, F_UNLCK, 0, 0);
    close(jointFile);
    lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
    close(file);
    sendInfo(connection_fd, message);
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
        int ok = saveCustomer(file, offset, &c);
        lockRecord(file, F_UNLCK, offset, sizeof(struct Customer));
        close(file);

        if(!ok)
        {
            sendInfo(connection_fd, "Unable to change name");
            return;
        }
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

        int ok = lockRecord(file, F_WRLCK, offset, sizeof(struct Employee)) != -1;
        if(ok)
        {
            pread(file, &emp, sizeof(emp), offset);
            strcpy(emp.firstName, newName);
            ok = saveRecord(file, offset, &emp, sizeof(emp));
        }
        lockRecord(file, F_UNLCK, offset, sizeof(struct Employee));
        close(file);

        if(!ok)
        {
            sendInfo(connection_fd, "Unable to change name");
            return;
        }
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

    int ok = lockRecord(file, F_WRLCK, offset, sizeof(struct Employee)) != -1;
    if(ok)
    {
        pread(file, &emp, sizeof(emp), offset);
        emp.role = (choice == 1) ? 0 : 1;
        ok = saveRecord(file, offset, &emp, sizeof(emp));
    }
    lockRecord(file, F_UNLCK, offset, sizeof(struct Employee));
    close(file);

    if(!ok)
    {
        sendInfo(connection_fd, "Unable to change role");
        return;
    }
    printf("Admin made %d %s\n", id, choice == 1 ? "manager" : "employee");
    sendInfo(connection_fd, "Role changed successfully");
}
