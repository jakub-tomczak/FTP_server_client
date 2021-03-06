//
// Created by jakub on 01.01.18.
//

#include <algorithm>
#include <unistd.h>
#include <iostream>
#include <iterator>
#include <sstream>
#include <arpa/inet.h>

//first method to display interface addr
#include <cstring>
#include <sys/ioctl.h>
#include <net/if.h> //IFNAMSIZ

//second method
#include <fstream>
#include "ServerConfig.h"

//my includes
#include "FTP.h"
#include "Directory.h"
#include "ServerException.h"
#include "TerminalUtils.h"

//static fields
vector<uint16_t> FTP::dataConnectionPorts;

FTP::FTP() {
    throw ServerException("Use FTP(int socket) instead.");
}

FTP::FTP(int socket) : socketDescriptor(socket) {
    currentDirectory = "/";
    dataConnectionPort = 0;
    dataConnectionOpened = false;
}

FTP::FTP(Client *client) {
    clientData = client;
}

FTP::~FTP() {
    killDataConnectionThreads();
    delete clientData;
}

string FTP::toUpper(string data) {
    std::transform(data.begin(), data.end(), data.begin(), ::toupper);
    return data;
}

void FTP::parseCommand(char *command) {
    string data(command);
    this->parseCommand(data);
}

void FTP::parseCommand(string command) {
    if (command.size() > 280) {
        throw ServerException("500 Komenda za długa.");
    }

    vector<string> splittedCommand = splitCommand(command);

    if (splittedCommand.empty()) {
        throw ServerException("500 Błąd w składni.");
    }

    splittedCommand[0] = toUpper(splittedCommand[0]);

    if (splittedCommand[0].find("TYPE") != string::npos) {
        if (splittedCommand.size() < 2) {
            throw ServerException("501 Brak oczekiwanego prametru.");
        }
        setTransferType(splittedCommand[1]);
    } else if (splittedCommand[0].find("MKD") != string::npos) {
        if (splittedCommand.size() < 2) {
            throw ServerException("501 Brak oczekiwanego prametru.");
        }
        string dirToCreate = getNameWithSpaces(splittedCommand) + '/';
        makeDirectory(dirToCreate);
    } else if (splittedCommand[0].find("RMD") != string::npos) {
        if (splittedCommand.size() < 2) {
            throw ServerException("501 Brak oczekiwanego prametru.");
        }
        string directoryToRemove = getNameWithSpaces(splittedCommand) + '/';
        removeDirectory(directoryToRemove);
    } else if (splittedCommand[0].find("LIST") != string::npos) {
        if (splittedCommand.size() < 2) {
            listFiles(currentDirectory);
        } else {
            string directory = getNameWithSpaces(splittedCommand) + '/';
            listFiles(directory);
        }
    } else if (splittedCommand[0].find("PWD") != string::npos) {
        //wypisz zawartrosc zmiennej currentDirectory
        printDirectory();
    } else if (splittedCommand[0].find("CWD") != string::npos) {
        if (splittedCommand.size() < 2) {
            changeDirectory("/");   //brak parametru, przejdz do glownego
        } else {
            string directory = getNameWithSpaces(splittedCommand) + '/';
            changeDirectory(directory);    //przejdz do wskazanego przez parametr
        }
    } else if (splittedCommand[0].find("PASV") != string::npos) {
        sendPASSVResponse();
    } else if (splittedCommand[0].find("RETR") != string::npos) {
        //wysylanie plików z serwera do klienta
        if (splittedCommand.size() < 2) {
            throw ServerException("501 Brak oczekiwanego prametru.");
        }
        string filename = getNameWithSpaces(splittedCommand);
        putFile(filename);
    } else if (splittedCommand[0].find("STOR") != string::npos) {
        //wysylanie plikow od klineta na serwer
        if (splittedCommand.size() < 2) {
            throw ServerException("501 Brak oczekiwanego prametru.");
        }
        string filename = getNameWithSpaces(splittedCommand);
        getFile(filename);

    } else if (splittedCommand[0].find("STATUS") != string::npos) {
        sendResponse("200 OK.");

    } else {
        throw ServerException("500 Komenda nierozpoznana.");
    }
}


void FTP::sendResponse(string message) {
    message += "\r\n";
    cout << "\t" << MAGENTA_TEXT("odpowiedź do " << socketDescriptor << ":\t") << GREEN_TEXT(message);
    write(socketDescriptor, message.c_str(), message.size());
}


void FTP::putFile(string filename) {
    Directory::slashesConverter(&filename);
    //test dla pliku w folderze glownym i podkatalogu ze wzgledu na currentDirectory string
    if (!Directory::isFileExist(Directory::getRootDir() + currentDirectory + filename)) {
        throw ServerException("550 Plik " + filename + " nie istnieje.");
    }
    if (dataConnectionPort == 0) {
        throw ServerException("500 Send PASV.");
    }
    if (!downloadThreadActive) {
        createThread(ThreadType::Upload);
    }
    cout << "Trying to send file " << filename << endl;
    sendResponse("226 Połączenie otwarte.");
    pthread_mutex_lock(&fileToUpload_mutex);
    fileToUpload = filename;
    pthread_mutex_unlock(&fileToUpload_mutex);

    pthread_mutex_lock(&tryToUploadFile_mutex);
    tryToUploadFile = true;
    pthread_mutex_unlock(&tryToUploadFile_mutex);
}

void FTP::getFile(string filename) {
    Directory::slashesConverter(&filename);
    if (dataConnectionPort == 0) {
        throw ServerException("500 Send PASV.");
    }
    if (!downloadThreadActive) {
        createThread(ThreadType::Download);
    }

    cout << "Trying to get file " << filename << endl;
    sendResponse("226 Połączenie otwarte.");
    pthread_mutex_lock(&fileToDownload_mutex);
    fileToDownload = filename;
    pthread_mutex_unlock(&fileToDownload_mutex);

    pthread_mutex_lock(&tryToDownloadFile_mutex);
    tryToDownloadFile = true;
    pthread_mutex_unlock(&tryToDownloadFile_mutex);

}

//directory methods
void FTP::removeDirectory(string name) {
    Directory::removeDirectory(name, currentDirectory);
    sendResponse("250 OK");
}

/*
 * Tworzy folder/foldery.
 * Składnia nazwaFolderu/[nazwa kolejnego folderu]/
 * Jeżeli dany folder istnieje to zwraca 200 OK
 * Zwraca wyjątek tylko w przypadku braku uprawnień do tworzenia folderu.
 *
 */
void FTP::makeDirectory(string name) {
    Directory::createDirectories(name, currentDirectory);
    sendResponse("257 OK");
}

//file transfer methods
void FTP::setTransferType(string type) {
    if (type.size() != 1) {
        throw ServerException("501 Błąd w składni parametrów lub argumentów.");
    }

    //to uppercase
    if (type[0] >= 'a')
        type[0] -= 32;

    switch (type[0]) {
        case 'A':
            transferType = 'A';
            sendResponse("200 Type set to A."); //tryb ASCII
            break;
        case 'I':
            transferType = 'I';
            sendResponse("200 Type set to I."); //tryb binary
            break;
        default:
            throw ServerException("501 Niewspierany tryb pracy.");
    }
}

/*
 * list /   -> listuje katalog głowny
 * list     -> listuje aktualny katalog
 * list dir -> listuje podkatalog dir
 */
void FTP::listFiles(string dirName) {
    string list = Directory::listFiles(dirName, currentDirectory);
    sendResponse(list);
}

/*
 * Zmienia bieżący katalog.
 * Parametr jest ścieżką bezwzględną.
 *
 */
void FTP::changeDirectory(string name) {
    currentDirectory = Directory::changeDirectory(name, currentDirectory);
    string reply = "250 ";
    reply += currentDirectory;
    sendResponse(reply);
}

/*
 * Wypisuje aktualny katalog, przy czym
 * będąc w katalogu głównym wypisuje -> /
 * będą poza katalogiem głównym nie wypisują wiodącego /
 * znajdując się w folderze folder1 wypisze folder1/
 */
void FTP::printDirectory() {
    sendResponse(currentDirectory);
}

//rozbija komende na zbior wyrazen, co spacje
vector<string> FTP::splitCommand(string command) {
    vector<string> pieces;
    istringstream iss(command);
    copy(istream_iterator<string>(iss),
         istream_iterator<string>(),
         back_inserter(pieces));
    return pieces;
}

//example /dir1/named/ dir/dir2,  "named/ dir" -> "named dir"
string FTP::getStringWithSpaces(vector<string> command) {
    //at index 0 is command string
    size_t iter = 1;
    string directory = command[iter];

    //checks if last char in a directory is a backslash that indicates a space
    while (command[iter][command[iter].size() - 1] == '\\') {
        //remove backslash from directory
        directory.erase(directory.size() - 1, 1);

        //check if there is another string to add
        if (command.size() - 1 >= (iter + 1)) {
            directory += char(32);  //add space
            directory += command[iter + 1];
        } else {
            break;
        }
        iter++;
    }

    //usun slash na koncu, aby funkcja byla bardziej uniwersalna - dla folderow i plikow
    if (directory[directory.size() - 1] == '/') {
        directory.erase(directory.size() - 1, 1);
    }
    if (directory[directory.size() - 1] == '\\') {
        directory.erase(directory.size() - 1, 1);
    }
    return directory;
}

string FTP::getNameWithSpaces(vector<string> command) {
    string name = "";
    for (uint i = 1; i < command.size(); i++) {
        name += command[i];
        if (i < command.size() - 1) {
            name += ' ';
        }
    }
    //usun slash na koncu, aby funkcja byla bardziej uniwersalna - dla folderow i plikow
    if (name[name.size() - 1] == '/') {
        name.erase(name.size() - 1, 1);
    }
    if (name[name.size() - 1] == '\\') {
        name.erase(name.size() - 1, 1);
    }
    return name;
}

/*
 * Jeżeli jest wysyłany drugi passv to staramy sie zamknac poprzedni port,
 * jeżeli jest to niemozliwe, rzucamy wyjatek.
 *
 */
void FTP::sendPASSVResponse() {
    if (uploadThreadActive || downloadThreadActive) {
        throw ServerException("500 0,0 Zmiana portu niemożliwa, port aktualnie w użyciu.");
    } else {
        if (dataConnectionSocket != 0) {
            pthread_mutex_lock(&dataConnectionOpened_mutex);
            dataConnectionOpened = false;
            pthread_mutex_unlock(&dataConnectionOpened_mutex);
            close(dataConnectionSocket);
        }
        //zabij watki, mozliwe, ze moga dalej sie starac otwierac poprzedni socket
        killDataConnectionThreads();
    }

    string defaultInterfaceAddr = getDefaultInterfaceAddr();
    string randomPort = getRandomPort();

    //kiedy port ustawiony otworz polaczenie, aby zagwarantowac, ze zaden inny program
    //nie odbierze nam portu
    setUpSocketForDataConnection();

    sendResponse("227 " + randomPort + ".");
}

string FTP::getDefaultInterfaceName() {
    system("ip route | awk '/default/ {printf $5}' >> eth");   //get default interface
    ifstream file("eth");
    string interfaceName;
    if (file.is_open()) {
        if (!file.eof()) {
            file >> interfaceName;
        }
        file.close();
        unlink("eth");
    }
    return interfaceName;
}

string FTP::getDefaultInterfaceAddr() {
    string interfaceName = getDefaultInterfaceName();

    int fd;
    struct ifreq ifr{};

    fd = socket(AF_INET, SOCK_DGRAM, 0);

    // I want to get an IPv4 IP address
    ifr.ifr_addr.sa_family = AF_INET;

    // I want IP address attached to "eth0"
    if (interfaceName.empty()) {
        interfaceName = DEFAULT_INTERFACE;
    }
    strncpy(ifr.ifr_name, interfaceName.c_str(), IFNAMSIZ - 1); //enp0s3

    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        interfaceName = DEFAULT_INTERFACE;
#if DEBUG
        printf("error while finding interface\n");
#endif
    }
    close(fd);

    string addr(inet_ntoa(((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr));

    size_t pos;
    while ((pos = addr.find('.')) != string::npos) {
        addr.replace(pos, 1, ",");
    }
#if false
    printf("error while finding interface\n");
#endif
    return addr;
}

bool FTP::isPortReserved(uint16_t port) {
    //check in list with ports
    pthread_mutex_lock(&dataConnectionPorts_mutex);
    for (auto const &value: dataConnectionPorts) {
        if (value == port) {
            return true;
        }
    }
    pthread_mutex_unlock(&dataConnectionPorts_mutex);


    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return true;
    }
    struct sockaddr_in sockAddr{};
    memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.sin_family = AF_INET;
    inet_pton(AF_INET, DEFAULT_ADDR, &sockAddr.sin_addr);
    sockAddr.sin_port = port;
    sockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    //bindowanie do socketu
    int time = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &time, sizeof(time));
    if (bind(sockfd, (struct sockaddr *) &sockAddr, sizeof(sockAddr)) < 0) {
        return true;
    }

    close(sockfd);

    return false;
}

string FTP::getRandomPort() {
    uint16_t port;
    uint16_t p1;
    uint16_t p2;
    srand(static_cast<unsigned int>(time(nullptr)));
    do {
        //port = p1 * 256 + p2
        //p1 * 256  -> [1024, 32 768]
        //p2        -> [0, 32 767]
        p1 = ((rand() % 125) + 4); //p1 -> [4, 128]
        p2 = ((rand() % (1 << 15)) - 1);
        port = (p1 << 8) + p2;
    } while (isPortReserved(port));

    //add port to global ports
    pthread_mutex_lock(&dataConnectionPorts_mutex);
    FTP::dataConnectionPorts.push_back(port);
    //set port in this instance
    pthread_mutex_unlock(&dataConnectionPorts_mutex);

    dataConnectionPort = port;

    string portStr;
    auto *temp = new char[20];
    sprintf(temp, "%d", p1);

    portStr = temp;
    portStr += ',';

    memset(temp, 0, 20);
    sprintf(temp, "%d", p2);
    portStr += temp;

    delete[]temp;
    return portStr;
}

int FTP::createThread(ThreadType threadType) {
    //tworzy watek dla serwera

    int create_result;
    switch (threadType) {
        case ThreadType::Download:
            create_result = pthread_create(&downloadThreadHandle, nullptr, newDownloadThreadWrapper, this);
            break;
        case ThreadType::Upload:
            create_result = pthread_create(&uploadThreadHandle, nullptr, newUploadThreadWrapper, this);
            break;
        default:
            throw ServerException("Unknown ThreadType when creating data connection thread.");
    }

    if (create_result != 0) {
        throw ServerException("Błąd przy próbie utworzenia wątku dla serwera, kod błędu: " + create_result);
    } else {
#if DEBUG
        cout << "Creted successfully " << (threadType == ThreadType::Upload ? " upload " : " download ")
             << "thread for client " << socketDescriptor << endl;
#endif
    }
    return create_result;
}

void FTP::release_thread(void *threadType) {
    ThreadType *type = (ThreadType *) threadType;
    switch (*type) {

        case ThreadType::Download:
#if DEBUG
            cout << "Thread download released.\n";
#endif
            break;

        case ThreadType::Upload:
#if DEBUG
            cout << "Thread upload released.\n";
#endif
            break;
    }
}

void *FTP::uploadThread(void *args) {
    ThreadType type = ThreadType::Upload;
    pthread_cleanup_push(release_thread, (void *) &type);
        pthread_mutex_lock(&uploadThreadActive_mutex);
        uploadThreadActive = true;
        pthread_mutex_unlock(&uploadThreadActive_mutex);
        if (!dataConnectionOpened) {
            setUpSocketForDataConnection();
        }

        //wait for connection from client
#if DEBUG
        cout << "Oczekiwanie na połączenie na porcie " << dataConnectionPort << endl;
#endif
        socklen_t sockSize = sizeof(struct sockaddr);
        int connection_descriptor = accept(dataConnectionSocket, (struct sockaddr *) &remote, &sockSize);
        if (connection_descriptor < 0) {
            perror("Client accepting error");
        }
        //zabezpieczenie w przypadku, gdy najpierw klient sie podlacza a nastepnie wysyla stor
        //wowczas nazwa pliku moze byc niepoprawna.
        pthread_mutex_lock(&tryToUploadFile_mutex);
        bool canDownload = tryToUploadFile;
        pthread_mutex_unlock(&tryToUploadFile_mutex);

        while (!canDownload) {
            usleep(100000); //wait 100ms
            pthread_mutex_lock(&tryToUploadFile_mutex);
            canDownload = tryToUploadFile;
            pthread_mutex_unlock(&tryToUploadFile_mutex);
        }
        pthread_mutex_lock(&tryToUploadFile_mutex);
        tryToUploadFile = false;
        pthread_mutex_unlock(&tryToUploadFile_mutex);

        //zapisnie adresu
        char remoteAddr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(remote.sin_addr), remoteAddr, INET_ADDRSTRLEN);
#if DEBUG
        cout << "Upload thread: Podłączono klienta z adresem " << remoteAddr << ". Przypisany deskryptor"
             << connection_descriptor << endl;
#endif

        pthread_mutex_lock(&fileToUpload_mutex);

        prepareFileToTransfer(&fileToUpload);
        string fileToUpload_localCopy = fileToUpload;
        unsigned int filesize = Directory::getSize(fileToUpload);
#if DEBUG
        cout << "Upload thread: Wielkość pliku do wysłania " << filesize << endl;
#endif

        fstream file;
        if (transferType == 'A') {
            file.open(fileToUpload, ios::in);
        } else {
            file.open(fileToUpload, ios::in | ios::binary);
        }
        pthread_mutex_unlock(&fileToUpload_mutex);
        int bytesSend = 0;

        if (!file.is_open()) {
            sendResponse("500 Nie znaleziono pliku do wysłania.");
        } else {
#if DEBUG
            cout << "Upload thread: Przygotowywanie pliku do wyslania " << fileToUpload_localCopy
                 << ". Wysylanie w trybie " << transferType << endl;
#endif
            bool connectionOpened = true;

            //buffer for data
            auto *buffer = new char[BUFFER_SIZE];
            while (connectionOpened) {
                while (!file.eof()) {
                    if (transferType == 'A') {
                        string line;
                        getline(file, line);
                        int addNewLine = file.eof() ? 0 : 1;

                        int bytesRead = line.size() + addNewLine;  //+1 == \n
                        if (addNewLine == 1)
                            line += '\n';

                        bytesSend += bytesRead;
                        write(connection_descriptor, line.c_str(), line.size());
                    } else {
                        //binary data
                        memset(buffer, 0, BUFFER_SIZE);
                        file.read(buffer, BUFFER_SIZE);
                        std::streamsize bytesRead = file.gcount();
                        bytesSend += bytesRead;
                        write(connection_descriptor, buffer, bytesRead);
                    }
                }
                connectionOpened = false;
            }
            file.flush();
            file.close();
        }

#if DEBUG
        cout << "Upload thread: Plik wysłany " << fileToUpload_localCopy
             << ". Wysłano " << bytesSend << " bajtów." << endl;
#endif
        sendResponse("226 Plik wysłany.");

        if (!downloadThreadActive) {
            //close socket only when data is not being downloaded
            //don't close dataConnectionSocket not to allow another app to reserve out port to data connection
            //close(dataConnectionSocket);
            close(connection_descriptor);
#if DEBUG
            cout << "Upload thread. Data connection closed - socket " << connection_descriptor << endl;
#endif

        }
        pthread_mutex_lock(&uploadThreadActive_mutex);
        uploadThreadActive = false;
        pthread_mutex_unlock(&uploadThreadActive_mutex);
    pthread_cleanup_pop(1);
    pthread_exit(nullptr);
}

void *FTP::downloadThread(void *args) {
    ThreadType type = ThreadType::Download;
    pthread_cleanup_push(release_thread, (void *) &type);
        pthread_mutex_lock(&downloadThreadActive_mutex);
        downloadThreadActive = true;
        pthread_mutex_unlock(&downloadThreadActive_mutex);
        if (!dataConnectionOpened) {
            setUpSocketForDataConnection();
        }

        //wait for connection from client
#if DEBUG
        cout << "Oczekiwanie na połączenie na porcie " << dataConnectionPort << endl;
#endif
        socklen_t sockSize = sizeof(struct sockaddr);
        int connection_descriptor = accept(dataConnectionSocket, (struct sockaddr *) &remote, &sockSize);
        if (connection_descriptor < 0) {
#if DEBUG
            cout << "Download thread, client " << socketDescriptor << " accepting error.\n";
#endif
            perror("Download thread.Client accepting error");
            //;
        }

        //zabezpieczenie w przypadku, gdy najpierw klient sie podlacza a nastepnie wysyla stor
        //wowczas nazwa pliku moze byc niepoprawna.
        pthread_mutex_lock(&tryToDownloadFile_mutex);
        bool canDownload = tryToDownloadFile;
        pthread_mutex_unlock(&tryToDownloadFile_mutex);

        while (!canDownload) {
            usleep(100000); //wait 100ms
            pthread_mutex_lock(&tryToDownloadFile_mutex);
            canDownload = tryToDownloadFile;
            pthread_mutex_unlock(&tryToDownloadFile_mutex);
        }
        pthread_mutex_lock(&tryToDownloadFile_mutex);
        tryToDownloadFile = false;
        pthread_mutex_unlock(&tryToDownloadFile_mutex);

        //zapisnie adresu
        char remoteAddr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(remote.sin_addr), remoteAddr, INET_ADDRSTRLEN);
#if DEBUG
        cout << "Download thread: Podłączono klienta z adresem " << remoteAddr << ". Przypisany deskryptor"
             << connection_descriptor << endl;
#endif

        pthread_mutex_lock(&fileToDownload_mutex);
        //add current directory
        prepareFileToTransfer(&fileToDownload);

        //stwórz lokalną kopię, aby użytkownik ustawiający nowy stor nie spowodował,
        //niespojnego komunikatu przed i po zapisie pliku
        string fileToDownload_localCopy = fileToDownload;
        fstream file;
        if (transferType == 'A') {
            file.open(fileToDownload, ios::out);
        } else {
            file.open(fileToDownload, ios::out | ios::binary);
        }

        pthread_mutex_unlock(&fileToDownload_mutex);
#if DEBUG
        cout << "Download thread: Tworzenie pliku " << fileToDownload_localCopy
             << endl;    //poza mutexem powinnismy korzysatc z lokalnej kopii
#endif
        bool connectionOpened = true;

        //buffer for data
        auto *buffer = new char[BUFFER_SIZE];
        while (connectionOpened) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t value = read(connection_descriptor, buffer, BUFFER_SIZE);
            if (value > -1) {
                if (value == 0) {
                    connectionOpened = false;
                    continue;
                } else {
                    file.write(buffer, value);
                }
            }
        }
        file.flush();
        file.close();
#if DEBUG
        cout << "Download thread: Plik zapisany " << fileToDownload_localCopy << endl;
#endif
        sendResponse("226 Plik odebrany.");
        if (!uploadThreadActive) {
            //close socket only when data is not being uploaded
            //close(dataConnectionSocket);
            //close socket from client
            close(connection_descriptor);
#if DEBUG
            cout << "Download thread. Data connection closed - socekt " << connection_descriptor << endl;
#endif

        }

        pthread_mutex_lock(&downloadThreadActive_mutex);
        downloadThreadActive = false;
        pthread_mutex_unlock(&downloadThreadActive_mutex);
    pthread_cleanup_pop(1);
    pthread_exit(nullptr);
}

void *FTP::newUploadThreadWrapper(void *object) {
    reinterpret_cast<FTP *>(object)->uploadThread(nullptr);
    return nullptr;
}

void *FTP::newDownloadThreadWrapper(void *object) {
    reinterpret_cast<FTP *>(object)->downloadThread(nullptr);
    return nullptr;
}

/*
 * Removes slash at the beginning.
 * Adds current directory position.
 */
void FTP::prepareFileToTransfer(string *file) {
    Directory::slashesConverter(file);
    //remove slash at 0 position
    if ((*file)[0] == '/') {
        file->erase(file->size() - 1, 1);
    }

    //add current directory
    *file = Directory::getRootDir() + (currentDirectory == "/" ? "" : currentDirectory) + *file;
}

//initiate socket for dataconnection
void FTP::setUpSocketForDataConnection() {
    //open new socket for the connection
    struct sockaddr_in sockAddr;
#if DEBUG
    cout << "Setting up socket for data connection\n";
#endif
    pthread_mutex_lock(&dataConnectionSocket_mutex);
    dataConnectionSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (dataConnectionSocket < 0) {
        printf("Client data connection socket. Socket error\n");
        throw ServerException("500 Internal server exception. Socket.");
    }
    memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(dataConnectionPort);
    sockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    //bindowanie do socketu
    int time = 1;
    setsockopt(dataConnectionSocket, SOL_SOCKET, SO_REUSEADDR, (char *) &time, sizeof(time));
    if (bind(dataConnectionSocket, (struct sockaddr *) &sockAddr, sizeof(sockAddr)) < 0) {
        perror("Client data connection socket. Binding error");
        throw ServerException("500 Internal server exception. Bind.");
    }

    if (listen(dataConnectionSocket, QUEUE_SIZE) < 0) {
        perror("Client data connection socket. Listen error");
        throw ServerException("500 Internal server exception. Listen.");
    }
#if DEBUG
    cout << "Client " << socketDescriptor << " socket for data connection initialized. Port " << dataConnectionPort
         << " binded.\n";
#endif
    pthread_mutex_unlock(&dataConnectionSocket_mutex);
    pthread_mutex_lock(&dataConnectionOpened_mutex);
    dataConnectionOpened = true;
    pthread_mutex_unlock(&dataConnectionOpened_mutex);

}

void FTP::killDataConnectionThreads() {
#if DEBUG
    cout << "Killing client's threads. Client descriptor " << socketDescriptor << endl;
#endif
    pthread_mutex_lock(&uploadThreadActive_mutex);
    if (uploadThreadHandle != 0 && uploadThreadActive) {
#if DEBUG
        cout << "Client " << socketDescriptor << " upload thread killed!" << endl;
#endif
        uploadThreadActive = false;
        pthread_cancel(uploadThreadHandle);
    }
    pthread_mutex_unlock(&uploadThreadActive_mutex);


    pthread_mutex_lock(&downloadThreadActive_mutex);
    if (downloadThreadHandle != 0 && uploadThreadActive) {
#if DEBUG
        cout << "Client " << socketDescriptor << " download thread killed!" << endl;
#endif
        downloadThreadActive = false;
        pthread_cancel(downloadThreadHandle);
    }
    pthread_mutex_unlock(&downloadThreadActive_mutex);


}