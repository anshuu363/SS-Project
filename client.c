#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/ip.h>
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<termios.h>
#include<arpa/inet.h>


void connectionHandler(int socketFileDescriptor);
void hide_input(char *buffer, int size);

// Usage: ./client            -> connects to the server on this machine
//        ./client 192.168.64.5 -> connects to a server on another machine (e.g. the UTM VM)
int main(int argc, char *argv[])
{
    char *serverIP = "127.0.0.1";
    if(argc > 1)
        serverIP = argv[1];

    int socketFileDescriptor;
    int connectStatus;

    struct sockaddr_in address;
    
    socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFileDescriptor == -1)
    {
        perror("Error\n");
        exit(-1);
    }
    printf("Client side socket successfully created!\n");

    // Convert the IP text (e.g. "127.0.0.1") into the binary form the socket needs
    if(inet_pton(AF_INET, serverIP, &address.sin_addr) != 1)
    {
        printf("Invalid server IP: %s\n", serverIP);
        exit(-1);
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);

    // Connecting to server
    connectStatus = connect(socketFileDescriptor, (struct sockaddr *)&address, sizeof(address));
    if(connectStatus == -1)
    {
        perror("Error\n");
        exit(-1);
    }

    // Handling server response
    connectionHandler(socketFileDescriptor);
    exit(0);
}

void connectionHandler(int socketFileDescriptor)
{
    char readBuffer[4096], writeBuffer[4096], tempBuffer[4096];
    int readBytes, writeBytes;

    do
    {
        bzero(readBuffer, sizeof(readBuffer));
        readBytes = read(socketFileDescriptor, readBuffer, sizeof(readBuffer));
        if(readBytes == -1)
        {
            printf("Unable to read from server\n");
        }
        else if(readBytes == 0)
        {
            printf("Closing the connection\n");
        }
        else
        {
            if (strcmp(readBuffer, "Enter password: ") == 0)
            {
                hide_input(writeBuffer, sizeof(writeBuffer));
            }            
            else
            {
                bzero(writeBuffer, sizeof(writeBuffer));
                bzero(tempBuffer, sizeof(tempBuffer));
                if(strcmp(readBuffer, "Client logging out...\n") == 0)
                {
                    strcpy(writeBuffer, "");
                    close(socketFileDescriptor);
                    return;
                }
                else if(strchr(readBuffer, '^') != NULL)
                {
                    if(strlen(readBuffer) != 1)
                    {
                        strncpy(tempBuffer, readBuffer, strlen(readBuffer) - 1);
                        printf("%s\n", tempBuffer);
                    }
                    strcpy(writeBuffer, "");
                }
                else
                {
                    printf("%s\n", readBuffer);
                    // Read the whole line (names may contain spaces), at most 4095 characters
                    scanf(" %4095[^\n]", writeBuffer);
                }                
            }
            
            writeBytes = write(socketFileDescriptor, writeBuffer, sizeof(writeBuffer));
            if(writeBytes == -1)
            {
                printf("Unable to write to server\n");
                printf("Closing the connection to the server now!\n");
                break;
            }
        }
    } while(readBytes > 0);

    close(socketFileDescriptor);
}

// Hiding Password
void hide_input(char *buffer, int size) {
    struct termios oldt, newt;
    
    // Get the current terminal settings
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;

    // Disable echoing of characters
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // Read input
    printf("Enter password: ");
    bzero(buffer, size);
    scanf(" %4095[^\n]", buffer);
    printf("\n");

    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}