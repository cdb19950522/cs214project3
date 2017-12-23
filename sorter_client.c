#include "sorter_client.h"
pthread_mutex_t pathlock = PTHREAD_MUTEX_INITIALIZER;
char coltosort[50], outname[500], coltype;
int tosort;
char paths[1000][300];
int pathcounter = 0;
char domain[100];
int port;
int maxsocket = 1;
sem_t socketsem;
void *traversedir(void *oripath)
{
    char *path = oripath;
    DIR *dp = opendir(path);
    pthread_t currtid;
    pthread_t waittid[600];
    int countthread = 0, i, localcounter = 0, joined = 0;
    struct dirent *entry;

    if (dp == NULL) {
        fprintf(stderr, "failed to read dir %s: %s\n", path, strerror(errno));
        return NULL;
    }
    while (entry = readdir(dp)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        /* ================================================================== */
        pthread_mutex_lock(&pathlock);
        localcounter = pathcounter++ % 1000;
        pthread_mutex_unlock(&pathlock);
        /* ================================================================== */
        sprintf(paths[localcounter], "%s/%s", path, entry->d_name);
        if (entry->d_type == DT_DIR) {
            pthread_create(&currtid, NULL, (void *)&traversedir,
                           (void *)&paths[localcounter]);
        } else {
            if (strlen(paths[localcounter]) < 4
                || strcmp(paths[localcounter] + strlen(paths[localcounter]) - 4,
                          ".csv") != 0
                || strstr(paths[localcounter], "-sorted-"))
                continue;
            pthread_create(&currtid, NULL, (void *)&transfer,
                           (void *)&paths[localcounter]);
        }
        waittid[countthread++] = currtid;
        if (countthread == 64) {
            for (i = 0; i < countthread; i++) {
                pthread_join(waittid[i], NULL);
            }
            countthread = 0;
        }
    }
    for (i = 0; i < countthread; i++) {
        pthread_join(waittid[i], NULL);
    }
    closedir(dp);
}

int createsocket()
{
    sem_wait(&socketsem);
    int sockfd;
    struct sockaddr_in address;
    struct hostent *server;

    server = gethostbyname(domain);
    if (server == NULL) {
        fprintf(stderr, "ERROR, NO SUCH HOST\n");
        exit(0);
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    bcopy((char *)server->h_addr, (char *)&address.sin_addr.s_addr,
          server->h_length);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    if (connect(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connect");
        releasesocket(sockfd);
        exit(EXIT_FAILURE);
    }
    return sockfd;
}
void releasesocket(int sockfd)
{
    close(sockfd);
    sem_post(&socketsem);
}
void *transfer(void *rawpath)
{
    char *filename = rawpath;

    /* ================================================================== */
    // check valid csv
    FILE *fp = fopen(filename, "r");
    char line[500];

    fgets(line, 500, fp);
    fclose(fp);
    int i, commas = 0;

    for (i = 0; i < strlen(line); i++)
        if (line[i] == ',')
            commas++;
    if (commas != 27)
        return NULL;
    /* ================================================================== */
    // create socket
    int sockfd = createsocket();

    /* ================================================================== */
    // transfer csv
    int filefd = open(filename, O_RDONLY);
    struct stat stat_buf;

    fstat(filefd, &stat_buf);
    char filelength[255];

    sprintf(filelength, "%d", stat_buf.st_size);
    send(sockfd, filelength, sizeof(filelength), 0);
  // printf("writing # bytes = %s\n", filelength);
    int remain = stat_buf.st_size, sent;
    off_t offset = 0;
    char buffer[255];

    read(sockfd, buffer, sizeof(buffer));
  // printf("%s\n", buffer);
    while (remain > 0
           && (sent =
               sendfile(sockfd, filefd, &offset, min(remain, BUFSIZ))) > 0) {
        remain -= sent;
      // printf("sent = %d\tremain = %d\n", sent, remain);
    }
  // printf("out\n");
    read(sockfd, buffer, sizeof(buffer));
  // printf("from server: %s\n", buffer);
    releasesocket(sockfd);
    close(filefd);
    return NULL;
}

void sendcolinfo()
{
    int sockfd = createsocket();
    char protocol[4];

    protocol[0] = '>';
    protocol[1] = coltype;
    protocol[2] = tosort;
    protocol[3] = '\0';
    send(sockfd, protocol, sizeof(protocol), 0);
    releasesocket(sockfd);
}

void mergerequest()
{
    int sockfd = createsocket();
    char protocol[2];

    protocol[0] = '<';
    protocol[1] = '\0';
    send(sockfd, protocol, sizeof(protocol), 0);
    char buffer[10000] = "";

    recv(sockfd, buffer, sizeof(buffer), 0);
    // tell server received length
    send(sockfd, protocol, sizeof(protocol), 0);
    int remain = atoi(buffer);
    char *file = (char *)malloc(remain);

    *file = '\0';
    ssize_t len;

  // printf("got all files: length = %d\n", remain);
    while (remain > 0
           && (len = recv(sockfd, buffer, min(remain, BUFSIZ), 0)) > 0) {
        strncat(file, buffer, min(remain, len));
        remain -= len;
        // printf("remain = %d\tread = %d\n", remain, len);
    }
  // printf("%s\n", file);
  // printf("got all files: length = %d\n", strlen(file));
    releasesocket(sockfd);
    FILE *outfp = fopen(outname, "w");

    fprintf(outfp,
            "color,director_name,num_critic_for_reviews,duration,director_facebook_likes,actor_3_facebook_likes,actor_2_name,actor_1_facebook_likes,gross,genres,actor_1_name,movie_title,num_voted_users,cast_total_facebook_likes,actor_3_name,facenumber_in_poster,plot_keywords,movie_imdb_link,num_user_for_reviews,language,country,content_rating,budget,title_year,actor_2_facebook_likes,imdb_score,aspect_ratio,movie_facebook_likes\n%s",
            file);
    fclose(outfp);
}
int main(int argc, char **argv)
{
    strcpy(coltosort, "director_name");
    char path[500] = ".", outpath[500] = ".";
    int c = 1;

    if (argc < 7) {
        fprintf(stderr,
                "usage %s -c <column to sort> -h <host> -p <port> -s <max num of socket> -d <search dir> -o <output dir>\n",
                argv[0]);
        return 0;
    }
    while (c < argc - 1) {
        if (*(argv[c] + 1) == 'c')
            strcpy(coltosort, argv[c + 1]);
        else if (*(argv[c] + 1) == 'd')
            strcpy(path, argv[c + 1]);
        else if (*(argv[c] + 1) == 'o')
            strcpy(outpath, argv[c + 1]);
        else if (*(argv[c] + 1) == 'h')
            strcpy(domain, argv[c + 1]);
        else if (*(argv[c] + 1) == 'p')
            port = atoi(argv[c + 1]);
        else if (*(argv[c] + 1) == 's')
            maxsocket = atoi(argv[c + 1]);
        c += 2;
    }
    sem_init(&socketsem, 0, maxsocket);
    getcolinfo();
    sendcolinfo();
    sprintf(outname, "%s/AllFiles-sorted-%s.csv", outpath, coltosort);
    /* ================================================================== */
    traversedir(path);
    // send merge request
    mergerequest();
    /* ================================================================== */
    pthread_mutex_destroy(&pathlock);
    return 0;
}
char isnum(char *s)
{
    if (strlen(s) == 0)
        return 's';
    int i;

    if ((s[0] >= '0' && s[0] <= '9') || s[0] == '-' || s[0] == '.') {
        for (i = 1; i < strlen(s); i++)
            if ((s[i] < '0' || s[i] > '9') && s[i] != '.')
                return 's';
    } else
        return 's';
    return 'n';
}

void getcolinfo()
{
    if (strcmp(coltosort, "color") == 0) {
        tosort = 0;
        coltype = 's';
    } else if (strcmp(coltosort, "director_name") == 0) {
        tosort = 1;
        coltype = 's';
    } else if (strcmp(coltosort, "num_critic_for_reviews") == 0) {
        tosort = 2;
        coltype = 'n';
    } else if (strcmp(coltosort, "duration") == 0) {
        tosort = 3;
        coltype = 'n';
    } else if (strcmp(coltosort, "director_facebook_likes") == 0) {
        tosort = 4;
        coltype = 'n';
    } else if (strcmp(coltosort, "actor_3_facebook_likes") == 0) {
        tosort = 5;
        coltype = 'n';
    } else if (strcmp(coltosort, "actor_2_name") == 0) {
        tosort = 6;
        coltype = 's';
    } else if (strcmp(coltosort, "actor_1_facebook_likes") == 0) {
        tosort = 7;
        coltype = 'n';
    } else if (strcmp(coltosort, "gross") == 0) {
        tosort = 8;
        coltype = 's';
    } else if (strcmp(coltosort, "genres") == 0) {
        tosort = 9;
        coltype = 's';
    } else if (strcmp(coltosort, "actor_1_name") == 0) {
        tosort = 10;
        coltype = 's';
    } else if (strcmp(coltosort, "movie_title") == 0) {
        tosort = 11;
        coltype = 's';
    } else if (strcmp(coltosort, "num_voted_users") == 0) {
        tosort = 12;
        coltype = 'n';
    } else if (strcmp(coltosort, "cast_total_facebook_likes") == 0) {
        tosort = 13;
        coltype = 'n';
    } else if (strcmp(coltosort, "actor_3_name") == 0) {
        tosort = 14;
        coltype = 's';
    } else if (strcmp(coltosort, "facenumber_in_poster") == 0) {
        tosort = 15;
        coltype = 'n';
    } else if (strcmp(coltosort, "plot_keywords") == 0) {
        tosort = 16;
        coltype = 's';
    } else if (strcmp(coltosort, "movie_imdb_link") == 0) {
        tosort = 17;
        coltype = 's';
    } else if (strcmp(coltosort, "num_user_for_reviews") == 0) {
        tosort = 18;
        coltype = 'n';
    } else if (strcmp(coltosort, "language") == 0) {
        tosort = 19;
        coltype = 's';
    } else if (strcmp(coltosort, "country") == 0) {
        tosort = 20;
        coltype = 's';
    } else if (strcmp(coltosort, "content_rating") == 0) {
        tosort = 21;
        coltype = 's';
    } else if (strcmp(coltosort, "budget") == 0) {
        tosort = 22;
        coltype = 'n';
    } else if (strcmp(coltosort, "title_year") == 0) {
        tosort = 23;
        coltype = 'n';
    } else if (strcmp(coltosort, "actor_2_facebook_likes") == 0) {
        tosort = 24;
        coltype = 'n';
    } else if (strcmp(coltosort, "imdb_score") == 0) {
        tosort = 25;
        coltype = 'n';
    } else if (strcmp(coltosort, "aspect_ratio") == 0) {
        tosort = 26;
        coltype = 'n';
    } else if (strcmp(coltosort, "movie_facebook_likes") == 0) {
        tosort = 27;
        coltype = 'n';
    } else {
        exit(1);
    }
}
