#include "sorter_server.h"
pthread_mutex_t threadlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t socketlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t datalock = PTHREAD_MUTEX_INITIALIZER;
int port;
int *socketfds, sockcounter;
char ****alldata, coltype;
int tosort, linelen[10000], collen = 28;
int filecounter = 0, allfilelen = 0;
pthread_t tids[50];
int tidcounter = 0;
void jointhreads()
{
    pthread_mutex_lock(&threadlock);
    int i;

    for (i = 0; i < tidcounter; i++) {
        pthread_join(tids[i], NULL);
    }
    tidcounter = 0;
    pthread_mutex_unlock(&threadlock);
}
void *receive(void *args)
{
    int *socketpointer = args;
    int client_socket = *socketpointer;
    char send_buf[256] = "server received file";
    char recv_buf[10000] = "";

    recv(client_socket, recv_buf, BUFSIZ, 0);
    /* ================================================================== */
    // read instruction
    if (*recv_buf == '<') {     // merge
        jointhreads();
        // printf("going to merge %d files, line = %d\n", filecounter, linelen[0]);
        pthread_mutex_lock(&datalock);
        kwaymerge();
        // printf("finished merging, going to send\n");
        senddata(client_socket);
        pthread_mutex_unlock(&datalock);
        return NULL;
    }
    pthread_mutex_lock(&threadlock);
    int localcounter = tidcounter++;

    pthread_mutex_unlock(&threadlock);
    tids[localcounter] = pthread_self();
    if (*recv_buf == '>') {     // get col info
        coltype = recv_buf[1];
        tosort = recv_buf[2];
        // printf("[s] col info received, closed client %d\n", client_socket);
        close(client_socket);
        return NULL;
    }
    /* ================================================================== */
    // get file
    int filelength = atoi(recv_buf);

    allfilelen += filelength;
    char *file = (char *)malloc(filelength);

    *file = '\0';
    char action[20] = "server received len";

    write(client_socket, action, sizeof(action));
    int remain = filelength;
    ssize_t len;

    while (remain > 0
           && (len = recv(client_socket, recv_buf, min(remain, BUFSIZ), 0)) > 0) {
        strncat(file, recv_buf, min(remain, len));
        remain -= len;
        // printf("remain = %d\tread = %d\n", remain, len);
    }
    file[filelength] = '\0';
    // printf("[r] String read:\n%s\n", file);
    // printf("filelength = %d\tstrlen = %d\n", filelength, strlen(file));
    write(client_socket, send_buf, sizeof(send_buf));
    // printf("[s] finished loading, closed client %d\n", client_socket);
    close(client_socket);
    /* ================================================================== */
    // parse and sort
    pthread_mutex_lock(&datalock);
    localcounter = filecounter++;
    pthread_mutex_unlock(&datalock);
    char ***table = readdata(file, localcounter);

    free(file);
    mergesort(table, 0, linelen[localcounter] - 1, coltype, tosort);
    /* ================================================================== */
    alldata[localcounter] = table;
    return NULL;
}
char ***readdata(char *file, int localcounter)
{
    int i, lineindex = 0, colindex = 0, lastletterindex, maxlen = 3000;
    char ***table = (char ***)malloc(sizeof(char **) * maxlen);
    int filelength = strlen(file), cindex = 419;        // skip column titles
    char c, *buffer = (char *)malloc(sizeof(char) * 10000);

    do {
        table[lineindex] = (char **)malloc(sizeof(char *) * collen);
        colindex = 0;
        do {                    // read a line
            i = 0;
            c = file[cindex++];
            /* ================================================================== */
            if (c == '"') {     // process double quotes
                do {
                    c = file[cindex++];
                    buffer[i++] = c;
                } while (cindex < filelength && c != '"');
                buffer[i - 1] = '\0';
            }
            /* ================================================================== */
            while (cindex < filelength && c == ' ')     // process trailing space
                c = file[cindex++];
            lastletterindex = -1;
            while (cindex < filelength && c != ',' && c != '\n' && c != EOF) {  // read a column
                if (c != ' ')
                    lastletterindex = i;
                buffer[i++] = c;
                c = file[cindex++];
            }
            buffer[lastletterindex + 1] = '\0';
            /* ================================================================== */
            table[lineindex][colindex] =
                (char *)malloc(sizeof(char) * strlen(buffer) + 1);
            strcpy(table[lineindex][colindex], buffer);
            colindex++;
        } while (colindex < 29 && c != '\n' && c != '\r');
        if (lineindex == maxlen - 1) {  // need to allocate more space
            maxlen += maxlen;
            table = (char ***)realloc(table, maxlen * sizeof(char **));
        }
        lineindex++;
    } while (cindex < filelength);
    /* ================================================================== */
    if (colindex == 29) {       // a weird testcase leads to infinite columns
        linelen[localcounter] = lineindex - 1;  // remove last line
    } else {
        linelen[localcounter] = lineindex;
    }
    /* ================================================================== */
    // free(table[lineindex - 1][0]);      // free column 0 in extra line
    // free(table[lineindex - 1]); // free extra line
    free(buffer);
    return table;
}
void senddata(int client_socket)
{
	printf("\nsending %d chars\n", allfilelen);
    char *file = (char *)malloc(sizeof(char) * allfilelen * 20);
    int i, t;
	char quotes[] = "\"";
	char quotesnewline[] = "\"\n";
	char quotescomma[] = "\",";
	char comma[] = ",";
	char newline[] = "\n";

    *file = '\0';
    for (i = 0; i < linelen[0]; i++) {
        for (t = 0; t < 27; t++) {
            if (strchr(alldata[0][i][t], ',')) {
                strcat(file, quotes);
                strcat(file, alldata[0][i][t]);
                strcat(file, quotescomma);
            } else {
                // if (t == tosort) {
                // printf("%d: %s\n", i, alldata[0][i][t]);
                // }
                strcat(file, alldata[0][i][t]);
                strcat(file, comma);
            }
            free(alldata[0][i][t]);
        }
        if (strchr(alldata[0][i][t], ',')) {
            strcat(file, quotes);
            strcat(file, alldata[0][i][t]);
            strcat(file, quotesnewline);
        } else {
            strcat(file, alldata[0][i][t]);
            strcat(file, newline);
        }
        free(alldata[0][i][t]);
        free(alldata[0][i]);
    }
    free(alldata[0]);
    // free(alldata); // should not free because only malloced once
    // printf("tosort = %d\n", tosort);
    // printf("going to send: %s\n", file);
    /* ================================================================== */
    char length[80];

    // printf("allfilelen = %d\n", allfilelen);
    allfilelen = strlen(file);
    // printf("strlen = %d\n", allfilelen);
    sprintf(length, "%d", allfilelen);
    write(client_socket, length, sizeof(length));
    // make sure client received length
    read(client_socket, length, sizeof(length));
    /* ================================================================== */
    // int remain = allfilelen, sent = 0;
    // while (remain > 0 && (sent = write(client_socket, file, min(remain, BUFSIZ))) > 0) {
    // remain -= sent;
    // printf("sent = %d\tremain = %d\n", sent, remain);
    // }
    send(client_socket, file, allfilelen, 0);
    /* ================================================================== */
    free(file);
    // printf("[s] merged sent, closed client %d\n", client_socket);
    close(client_socket);
    /* ================================================================== */
    filecounter = 0;
    // allfilelen = 0;
    // sockcounter = 0;
}
int main(int argc, char **argv)
{
    alldata = (char ****)malloc(sizeof(char ***) * 10000);
    socketfds = (int *)malloc(sizeof(int) * 50000);
    int c = 1;

    if (argc < 3) {
        fprintf(stderr, "usage %s -p <port>\n", argv[0]);
        return 0;
    }
    while (c < argc - 1) {
        if (*(argv[c] + 1) == 'p')
            port = atoi(argv[c + 1]);
        c += 2;
    }
    /* ================================================================== */
    int server_sock, client_sock;
    struct sockaddr_in address;
    struct sockaddr_in client_address;
    int client_len = sizeof(client_address);
    char ipaddress[100];

    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) <= 0)   // -1 if failed
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    if (listen(server_sock, 0) < 0) {
        perror("listen");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    // printf("Waiting for connections...\n");
    /* ================================================================== */
    printf("Received connections from: ");
    while (1) {
        if ((client_sock =
             accept(server_sock, (struct sockaddr *)&client_address, &client_len)) < 0) {
            perror("accept");
            close(server_sock);
            exit(EXIT_FAILURE);
        }
        inet_ntop(AF_INET, &client_address.sin_addr.s_addr, ipaddress, sizeof(ipaddress));
        printf("%s,", ipaddress);
        fflush(stdout);
        // printf("[+] Connect to client %d\n", client_sock);
        pthread_mutex_lock(&socketlock);
        int localcounter = sockcounter++;

        pthread_mutex_unlock(&socketlock);
        socketfds[localcounter] = client_sock;
        pthread_t tid;

        pthread_create(&tid, NULL, (void *)&receive, (void *)&socketfds[localcounter]);
        // pthread_join(tid, NULL);
        if (tidcounter > 50) {
            jointhreads();
        }
    }
    close(server_sock);
    return 0;
}

void kwaymerge()
{
    int skip = 1, i;
    int context[1][2];

    while (skip < filecounter) {
        for (i = 0; i < filecounter - skip; i += skip * 2) {
            context[0][0] = i;
            context[0][1] = i + skip;
            twowaymerge(&context);
        }
        skip *= 2;
    }
}
void *twowaymerge(void *context)
{
    int *file = context, file1 = file[0], file2 = file[1];
    int totallen = linelen[file1] + linelen[file2], i = 0, j = 0, k = 0;
    char ***newfile = (char ***)malloc(sizeof(char **) * totallen);
    double comp;

    while (i < linelen[file1] && j < linelen[file2]) {
        if (coltype == 's')
            comp = strcmp(alldata[file1][i][tosort], alldata[file2][j][tosort]);
        else
            comp = atof(alldata[file1][i][tosort]) - atof(alldata[file2][j][tosort]);
        if (comp <= 0)
            newfile[k++] = alldata[file1][i++];
        else
            newfile[k++] = alldata[file2][j++];
    }
    while (i < linelen[file1])
        newfile[k++] = alldata[file1][i++];
    while (j < linelen[file2])
        newfile[k++] = alldata[file2][j++];
    free(alldata[file1]);
    free(alldata[file2]);
    alldata[file1] = newfile;
    linelen[file1] = totallen;
    linelen[file2] = 0;
}
