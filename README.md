# Online Banking Management System (EG 301 Operating Systems Mini Project)

A client–server banking system in C for Linux. The server keeps all data in files and serves many clients at the same time (one process per client).

## OS concepts used
| Concept | Where |
|---|---|
| Socket programming | `server.c` (`socket`, `bind`, `listen`, `accept`), `client.c` (`connect`) |
| Process management | `fork()` for each client, `_exit()`, `wait()` |
| Multithreading | Reaper thread in `server.c` (`pthread_create`, `pthread_mutex`) |
| File management | `open`, `read`, `write`, `lseek`, `close` on the `.txt` data files |
| File locking | `fcntl` record locks: read lock for viewing, write lock for deposit/withdraw/transfer |
| IPC | Named POSIX semaphores: one login session per user |
| Signals | `SIGINT`/`SIGTERM`/… handlers free the login, `SIGPIPE` ignored |

## Login types
1. **Customer**: normal account, logs in with account number and password
2. **Joint Account**: each holder has their own holder ID and password; both can be logged in at the same time on the same account
3. **Employee**: adds customers, processes assigned loans
4. **Manager**: activates/deactivates accounts, assigns loans, reads feedback
5. **Admin**: password `admin@123`; adds, deletes, modifies and searches accounts, adds employees, changes roles

The same user cannot be logged in from two sessions at the same time. Different users can.

## Running on a MacBook in a UTM Linux VM

### 1. Install the compiler (Ubuntu/Debian VM)
```bash
sudo apt update
sudo apt install build-essential libcrypt-dev git
```

### 2. Get the code and build it
```bash
git clone https://github.com/anshuu363/SS-Project.git
cd SS-Project
make
```
Build inside the VM. Apple Silicon Macs run ARM Linux VMs, so programs compiled elsewhere will not run there.

### 3. Run
Terminal 1:
```bash
./server
```
Terminal 2, 3, … (one per client):
```bash
./client
```
Always start the programs from the project folder, because the data files are read from the current folder.

### Optional: client on the Mac, server in the VM
In the VM, find its IP with `ip addr` (usually `192.168.64.x` with UTM's "Shared Network"). Then on the Mac:
```bash
gcc client.c -o client
./client 192.168.64.x
```

## First-time setup (create test users)
1. Client → `5` (Admin Login) → password `admin@123`
2. `1` Register New Employee, e.g. ID `501`
3. `1` again for a second employee `601`, then `6` Manage Roles → make `601` a manager
4. `2` Add Customer Account → type `1` (Normal), e.g. account `101`
5. `2` Add Customer Account → type `2` (Joint), e.g. account `200` with holder IDs `1` and `2`

## Things to try
- Log in as customer `101` in two clients: the second is refused ("already logged in from another session")
- Log in as `101` and `102` at the same time: both work
- Log in as joint holders `1` and `2` at the same time and deposit/withdraw from both: the balance stays correct
- Press `Ctrl+C` in a logged-in client, then log in again: it works

## Files
| File | Contents |
|---|---|
| `server.c` | Main server: sockets, fork, thread, main menu |
| `client.c` | Client program |
| `common_functions.h` | Helpers: sessions (semaphores), locking, reading/writing to the client |
| `customer_functions.h` | Customer and joint account login and menu |
| `employee_functions.h`, `manager_functions.h`, `admin_functions.h` | Staff menus |
| `bank_fun.h` | Record structures |
| `customers.txt`, `jointHolders.txt`, `employees.txt`, `loanDetails.txt`, `loanCounter.txt`, `trans_hist.txt`, `feedback.txt` | Data files (binary records) |
