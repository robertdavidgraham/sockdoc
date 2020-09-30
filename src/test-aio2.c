#include <fcntl.h> 
#include <stdlib.h> 
#include <stdio.h> 
#include <errno.h> 
#include <signal.h>

#include <unistd.h> 
#include <aio.h> 

#define BUF_SIZE 20 /* Size of buffers for read operations */


/* Application-defined structure for tracking 
 * I/O requests */ 
struct ioRequest { 
    int           reqNum; 
    int           status; 
    struct aiocb *aiocbp; 
};

/* On delivery of SIGQUIT, we attempt to 
 * cancel all outstanding I/O requests */
static volatile sig_atomic_t gotSIGQUIT = 0; 

/* Handler for SIGQUIT */ 
static void quitHandler(int sig)
{ 
    gotSIGQUIT = 1; 
}

/* Handler for I/O completion signal */ 
static void aioSigHandler(int sig, siginfo_t *si, void *ucontext) 
{ 
    if (si->si_code == SI_ASYNCIO) { 
        printf("I/O completion signal received\n");

        /* The corresponding ioRequest structure would be available as
               struct ioRequest *ioReq = si->si_value.sival_ptr; 
           and the file descriptor would then be available via 
               ioReq->aiocbp->aio_fildes */ 
    } 
}

int main(int argc, char *argv[])
{ 
    struct ioRequest *ioList; 
    struct aiocb *aiocbList; 
    struct sigaction sa; 
    int s, j; 
    int request_count;        /* Total number of queued I/O requests */
    int open_count;       /* Number of I/O requests still in progress */
    int err;

    if (argc < 2) { 
        fprintf(stderr, "Usage: %s <pathname> <pathname>...\n", 
                argv[0]); 
        exit(EXIT_FAILURE); 
    }
    request_count = argc - 1;


    /* Allocate our arrays */
    ioList = calloc(request_count, sizeof(struct ioRequest)); 
    if (ioList == NULL) 
        abort();
    aiocbList = calloc(request_count, sizeof(struct aiocb)); 
    if (aiocbList == NULL) 
        abort();


    /* Establish handlers for SIGQUIT and the I/O completion signal */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; 
    sa.sa_handler = quitHandler; 
    if (sigaction(SIGQUIT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO; 
    sa.sa_sigaction = aioSigHandler; 
    if (sigaction(SIGUSR1, &sa, NULL) == -1) { 
        perror("sigaction");
        exit(1);
    }


    /* Open each file specified on the command line, and queue 
       a read request on the resulting file descriptor */
    for (j = 0; j < request_count; j++) { 
        ioList[j].reqNum = j; 
        ioList[j].status = EINPROGRESS; 
        ioList[j].aiocbp = &aiocbList[j];
        ioList[j].aiocbp->aio_fildes = open(argv[j + 1], O_RDONLY); 
        if (ioList[j].aiocbp->aio_fildes == -1) {
            perror(argv[j + 1]);
            exit(1);
        }
        printf("opened %s on descriptor %d\n", argv[j + 1], 
                ioList[j].aiocbp->aio_fildes);


        ioList[j].aiocbp->aio_buf = malloc(BUF_SIZE); 
        if (ioList[j].aiocbp->aio_buf == NULL) 
            abort();
            

        ioList[j].aiocbp->aio_nbytes = BUF_SIZE; 
        ioList[j].aiocbp->aio_reqprio = 0; 
        ioList[j].aiocbp->aio_offset = 0; 
        ioList[j].aiocbp->aio_sigevent.sigev_value.sival_ptr = 
                                &ioList[j];


        s = aio_read(ioList[j].aiocbp); 
        if (s == -1) {
            perror("aio_read");
            exit(1);
        }
    }


    open_count = request_count;


    /* Loop, monitoring status of I/O requests */
    while (open_count > 0) { 
        sleep(3);       /* Delay between each monitoring step */


        if (gotSIGQUIT) {


            /* On receipt of SIGQUIT, attempt to cancel each of the
               outstanding I/O requests, and display status returned
               from the cancellation requests */


            printf("got SIGQUIT; canceling I/O requests: \n");


            for (j = 0; j < request_count; j++) { 
                if (ioList[j].status == EINPROGRESS) { 
                    printf("    Request %d on descriptor %d:", j,
                            ioList[j].aiocbp->aio_fildes); 
                    s = aio_cancel(ioList[j].aiocbp->aio_fildes, 
                            ioList[j].aiocbp); 
                    if (s == AIO_CANCELED) 
                        printf("I/O canceled\n"); 
                    else if (s == AIO_NOTCANCELED) 
                        printf("I/O not canceled\n"); 
                    else if (s == AIO_ALLDONE) 
                        printf("I/O all done\n"); 
                    else 
                        perror("aio_cancel"); 
                } 
            }


            gotSIGQUIT = 0; 
        }


        /* Check the status of each I/O request that is still 
           in progress */


        printf("aio_error():\n"); 
        for (j = 0; j < request_count; j++) { 
            if (ioList[j].status == EINPROGRESS) { 
                printf("    for request %d (descriptor %d): ", 
                        j, ioList[j].aiocbp->aio_fildes); 
                ioList[j].status = aio_error(ioList[j].aiocbp);


                switch (ioList[j].status) { 
                case 0: 
                    printf("I/O succeeded\n"); 
                    break; 
                case EINPROGRESS: 
                    printf("In progress\n"); 
                    break; 
                case ECANCELED: 
                    printf("Canceled\n"); 
                    break; 
                default: 
                    perror("aio_error"); 
                    break; 
                }


                if (ioList[j].status != EINPROGRESS) 
                    open_count--; 
            } 
        } 
    }


    printf("All I/O requests completed\n");


    /* Check status return of all I/O requests */


    printf("aio_return():\n"); 
    for (j = 0; j < request_count; j++) { 
        ssize_t s;


        s = aio_return(ioList[j].aiocbp); 
        printf("    for request %d (descriptor %d): %zd\n", 
                j, ioList[j].aiocbp->aio_fildes, s); 
    }


    exit(EXIT_SUCCESS); }