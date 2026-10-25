struct Customer {
    int accountNumber;
    float balance;
    char customerName[20],password[256];
    int activeStatus; // 0 -> deactivate, 1 -> activate
};


// One holder of a joint account. Every holder has an own ID and password,
// but all holders of the same accountNumber share one balance in customers.txt
struct JointHolder{
    int holderID, accountNumber;
    char name[20], password[256];
};


struct Employee{
    int empID;
    char firstName[20], lastName[20], password[256];
    int role; // 0 -> Manager, 1 -> Employee
};


struct LoanDetails{
    int empID, accountNumber;
    int loanID, loanAmount;
    int status; // 0 -> requested, 1 -> pending, 2 -> approved, 3 -> rejected
};


struct trans_histroy{
    int acc_no;
    char hist[1024];
};


struct FeedBack{
    char feedback[1024];
};


struct Counter{
    int count;
};