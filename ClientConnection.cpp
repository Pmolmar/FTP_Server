//****************************************************************************
//                         REDES Y SISTEMAS DISTRIBUIDOS
//
//                     2º de grado de Ingeniería Informática
//
//              This class processes an FTP transactions.
//
//****************************************************************************

#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cerrno>
#include <netdb.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <locale.h>
#include <langinfo.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <iostream>
#include <dirent.h>
#include <fstream>

#include "common.h"

#include "ClientConnection.h"
#include "FTPServer.h"

ClientConnection::ClientConnection(int s)
{
    int sock = (int)(s);

    char buffer[MAX_BUFF];

    control_socket = s;
    // Check the Linux man pages to know what fdopen does.
    fd = fdopen(s, "a+");
    if (fd == NULL)
    {
        std::cout << "Connection closed" << std::endl;

        fclose(fd);
        close(control_socket);
        ok = false;
        return;
    }

    ok = true;
    data_socket = -1;
};

ClientConnection::~ClientConnection()
{
    fclose(fd);
    close(control_socket);
}

int connect_TCP(uint32_t address, uint16_t port)
{
    struct sockaddr_in sin;
    struct hostent *hent;
    int s;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = address;

    // if (hent = gethostbyname(address))
    //     memcpy(&sin.sin_addr, hent->h_addr, hent->h_length);
    // else if ((sin.sin_addr.s_addr = inet_addr((char *)address)) == INADDR_NONE)
    //     errexit("No puedo resolver el nombre \"%s\"\n", address);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        errexit("No se puede crear el socket: %s\n", strerror(errno));

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
        errexit("No se puede conectar con %s: %s\n", address, strerror(errno));

    return s;
}

void ClientConnection::stop()
{
    close(data_socket);
    close(control_socket);
    parar = true;
}

#define COMMAND(cmd) strcmp(command, cmd) == 0

// This method processes the requests.
// Here you should implement the actions related to the FTP commands.
// See the example for the USER command.
// If you think that you have to add other commands feel free to do so. You
// are allowed to add auxiliary methods if necessary.

void ClientConnection::WaitForRequests()
{
    if (!ok)
    {
        return;
    }

    fprintf(fd, "220 Service ready\n");

    while (!parar)
    {
        fscanf(fd, "%s", command);

        if (COMMAND("USER"))
        {
            fscanf(fd, "%s", arg);
            fprintf(fd, "331 User name ok, need password.\n");
        }
        else if (COMMAND("PWD"))
        {
            fprintf(fd, "257 \"PATHNAME\" created.\"%s\" \n", get_current_dir_name());
        }
        else if (COMMAND("PASS"))
        {
            fscanf(fd, "%s", arg);
            if (strcmp(arg, "123") == 0)
                fprintf(fd, "230 User logged in, proceed.\n");
            else
            {
                fprintf(fd, "ERROR.\n");
                stop();
            }
        }
        else if (COMMAND("PORT"))
        {
            int h0, h1, h2, h3, p0, p1;
            fscanf(fd, "%d,%d,%d,%d,%d,%d", &h0, &h1, &h2, &h3, &p0, &p1);
            uint32_t addr = h3 << 24 | h2 << 16 | h1 << 8 | h0;
            uint16_t port = p0 << 8 | p1;
            data_socket = connect_TCP(addr, port);
            fprintf(fd, "200 OK. \n");
        }
        else if (COMMAND("PASV"))
        {
            int s, h0, h1, h2, h3;
            uint8_t p0, p1;
            uint16_t port;
            sockaddr_in sin;
            socklen_t slen = MAX_BUFF;

            s = define_socket_TCP(0);
            getsockname(s, (struct sockaddr *)&sin, &slen);

            port = sin.sin_port;
            h0 = 127;
            h1 = h2 = 0;
            h3 = 1;
            p0 = port & 0xFF;
            p1 = port >> 8;

            fprintf(fd, "227 Entering passive mode. \"%d, %d, %d, %d, %d, %d\"  \n", h0, h1, h2, h3, p0, p1);
            fflush(fd);
            data_socket = accept(s, (struct sockaddr *)&sin, &slen);
        }
        else if (COMMAND("CWD"))
        {
            fscanf(fd, "%s", arg);
            chdir(arg);
            fprintf(fd, "250 Requested file action okay, completed. \n");
        }
        else if (COMMAND("STOR")) //importante
        {
            //abrir fichero read binario
            fscanf(fd, "%s", arg);
            std::fstream file;
            file.open(arg, std::ios::binary | std::ios::out);

            if (!file.good())
                fprintf(fd, "550 Requested action not taken. File unavailable (e.g., file not found, no access). \n");
            else
            {
                fprintf(fd, "150 File creation okay \n");

                while (1)
                {
                    char buffer[MAX_BUFF];
                    int bytes;

                    recv(data_socket, buffer, MAX_BUFF, 0);
                    file << buffer;
                    bytes=file.gcount();

                    if (bytes != MAX_BUFF)
                        break;
                }

                fprintf(fd, "226 Closing data connection. Requested file action successful (for example, file transfer or file abort). \n");
                close(data_socket);
                file.close();
            }
        }
        else if (COMMAND("SYST"))
        {
            fprintf(fd, "215 Unix type is: L8.\n");
        }
        else if (COMMAND("TYPE"))
        {
            fscanf(fd, "%s", arg);
            fprintf(fd, "200 OK. \n");
        }
        else if (COMMAND("RETR")) //importante
        {
            //abrir fichero read binario
            fscanf(fd, "%s", arg);
            std::ifstream file;
            file.open(arg, std::ios::binary | std::ios::in);

            if (!file.good())
                fprintf(fd, "550 Requested action not taken. File unavailable (e.g., file not found, no access). \n");
            else
            {
                fprintf(fd, "125 Data connection already open; transfer starting \n");

                while (1)
                {
                    char buffer[MAX_BUFF];
                    int bytes;

                    file.read(buffer, MAX_BUFF);
                    bytes = file.gcount();

                    send(data_socket, buffer, bytes, 0);

                    if (bytes != MAX_BUFF)
                        break;
                }

                fprintf(fd, "226 Closing data connection. Requested file action successful (for example, file transfer or file abort). \n");
                close(data_socket);
                file.close();
            }
        }
        else if (COMMAND("QUIT"))
        {
            fprintf(fd, "221 Service closing control connection. Logged out if appropriate.. \n");
            fflush(fd);
            stop();
        }
        else if (COMMAND("LIST")) //importante
        {
            DIR *dir;
            struct dirent *direntp;
            std::string aux;
            fprintf(fd, "150 File status okay; about to open data connection.\n");

            dir = opendir(get_current_dir_name());

            while ((direntp = readdir(dir)) != NULL)
            {
                aux += direntp->d_name;
                aux += "\n";
            }
            send(data_socket, aux.c_str(), aux.size(), 0);
            closedir(dir);
            close(data_socket);
            fprintf(fd, "250 Requested file action okay, completed.\n");
        }
        else
        {
            fprintf(fd, "502 Command not implemented.\n");
            fflush(fd);
            printf("Comando : %s %s\n", command, arg);
            printf("Error interno del servidor\n");
        }
    }

    fclose(fd);

    return;
};
