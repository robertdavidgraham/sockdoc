/*
    benchmark POSIX asynchronous file I/O

    This program is a quick-and-dirty benchmark of the POSIX AIO
    subystem that should work on various platforms (Linux, macOS,
    FreeBSD, Solaris, and so on). It is intended more of a way
    of testing the AIO subystem itself rather than benchmarking
    the underlying hardware, for which tools like 'fio' or 'iozone'
    are better suited.

    In particular, this is meant as a demonstration on how to program
    using the AIO APIs.
*/
#define _FILE_OFFSET_BITS = 64
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <aio.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* write() */


/**
 * This structure contains all the configuration settings for
 * this program, as read in from the command-line, or set
 * in 'main()' as the defaults.
 */
struct config
{
    char *filename;
    off_t filesize;
    size_t queue_depth;
    size_t read_length;
    size_t alignment;
    struct timespec dispatch_timeout;
    unsigned long long max_io_count;
    time_t max_io_time;
    unsigned long long resolution;
};

/**
 * This structure shows the timing results, with 100 different
 * buckets, which depend upon the 'resolution'. By default
 * the resolution is 10-microseconds, which gives a range
 * from 0 to 1000 microseconds, at 10-microsecond intervals.
 * If there are too many results at 0 or 100, then you'll need
 * to re-run the program at a higher or lower resolution respectively.
 */
struct timings
{
    unsigned buckets[1001];
};

/**
 * The way AOI works is that for every outstanding transaction, you need
 * a control-block for the operating system, and a control block for your
 * own code that tracks it. In our case, our needs are simply, just
 * recording the timestamps for the operations.
 */
struct mycontrolblock {
    unsigned long long start;
    unsigned long long done;
};

/****************************************************************************
 * Get the highest resolution timestamp for the system, for timing how
 * long asynchronous events take.
 ****************************************************************************/ 
static unsigned long long
get_timestamp(void)
{
    struct timespec ts;

#if defined(CLOCK_UPTIME_RAW) /* macOS */
    clock_gettime(CLOCK_UPTIME_RAW, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#elif defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#else
#error Unknown clock source
    return time(0) * 1000000000ULL;
#endif

}

/****************************************************************************
 ****************************************************************************/ 
static void
timings_record(struct timings *t, unsigned long long resolution, unsigned long long elapsed)
{
    /* Convert nanoseconds to our desired resolution range, which
     * is by default 10-microseconds */
    elapsed /= resolution;
    if (elapsed >= 100ULL)
        elapsed = 100ULL;
    
    t->buckets[elapsed]++;
}

/****************************************************************************
 ****************************************************************************/ 
void
cfg_set_parameter(struct config *cfg, const char *name, const char *value)
{
    if (strcmp(name, "filename") == 0) {
        free(cfg->filename);
        cfg->filename = strdup(value);
    } else if (strcmp(name, "filesize") == 0) {
        cfg->filesize = strtoul(value, (char**)&value, 0);
        switch (*value) {
            case 'k': case 'K':
                cfg->filesize *= 1024ULL;
                break;
            case 'm': case 'M':
                cfg->filesize *= 1024ULL * 1024ULL;
                break;
            case 'g': case 'G':
                cfg->filesize *= 1024ULL * 1024ULL * 1024ULL;
                break;
            case 't': case 'T':
                cfg->filesize *= 1024ULL * 1024ULL * 1024ULL * 1024ULL;
                break;
            default:
                fprintf(stderr, "[-] unknown size: %s\n", value);
                exit(1);
        }
    } else {
        fprintf(stderr, "[-] unknown parm: %s\n", name);
    }
}

/****************************************************************************
 ****************************************************************************/ 
void
cfg_parse_command_line(struct config *cfg, int argc, char *argv[])
{
    int i;

    for (i=1; i<argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-') {
            if (strchr(argv[i], '=')) {
                char *name = strdup(argv[i] + 2);
                const char *value = strchr(name,'=') + 1;
                *strchr(name, '=') = '\0';
                cfg_set_parameter(cfg, name, value);
                free(name);
            } else if (strchr(argv[i], '=')) {
                char *name = strdup(argv[i]+2);
                const char *value = strchr(name,'=') + 1;
                *strchr(name, '=') = '\0';
                cfg_set_parameter(cfg, name, value);
            } else if (i + 1 < argc) {
                const char *name = argv[i] + 2;
                const char *value = argv[i+1];

                if (value[0] == '-' && value[1] != '\0')
                    value = "";
                cfg_set_parameter(cfg, name, value);
            } else {
                cfg_set_parameter(cfg, argv[i]+2, "");
            }
        } else if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'f':
                {
                    const char *value = argv[i]+2;
                    if (*value == '\0') {
                        i++;
                        if (i >= argc || argv[i][0] == '-') {
                            fprintf(stderr, "[-] expected filename after -f \n");
                            exit(1);
                        }
                        value = argv[i];
                    }
                    cfg_set_parameter(cfg, "filename", value);
                    break;
                }
                default:
                    fprintf(stderr, "[-] unknwon parameter: %c\n", argv[i][1]);
                    exit(1);
                    break;
            }
        } else {
            fprintf(stderr, "[-] unknown option: %s\n", argv[i]);
            exit(1);
        }
    }
}

/****************************************************************************
 * Do the initialiation of an asynchronous read request that is specific
 * to our application, as opposed to specific to the operating system.
 ****************************************************************************/ 
static void
mycb_read(struct aiocb *a, struct mycontrolblock *mycb, off_t filesize, int fd, size_t read_length)
{
    /* Get a random offset within the file */
    off_t offset = rand()<<15ULL | rand()<<30ULL | rand();
    offset = ((unsigned long long)offset) % filesize;
    
    /* Setup the asynchronous request structure */
    a->aio_fildes = fd; 
    a->aio_nbytes = read_length;
    a->aio_offset = offset;


    /* Execute the read. This starts the read process, but doesn't finish
     * it. We'll finish it later */
    int err;
    err = aio_read(a);
    if (err) {
        fprintf(stderr, "[-] aio_read: %d: %s\n", (int)errno, strerror(errno));
        exit(1);
    }

    mycb->start = get_timestamp();
}

/****************************************************************************
 ****************************************************************************/ 
void
mycb_read_done(struct mycontrolblock *mycb, off_t offset, const void *buf, size_t buf_len)
{
    size_t i;
    
    /* verify contents of the read request */
    for (i=0; i<buf_len; i++) {
        unsigned x = (offset + i) & 0xFF;
        if (x != ((const unsigned char *)buf)[i]) {
            printf(".");
        }
    }
    mycb->done = get_timestamp();
}

/****************************************************************************
 ****************************************************************************/ 
static int
util_file_disable_caching(int fd)
{
#if defined(F_NOCACHE)
    /* macOS */
    int err = fcntl(fd, F_NOCACHE, 1);
    if (err == -1) {
        perror("fcntl(F_NOCACHE)");
        exit(1);
    }
    return 0;
#elif defined(O_DIRECT)
    int err;
    int flags;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        fprintf(stderr, "[-] fcntl(F_FETFL): %d: %s\n", errno, strerror(errno));
        return -1;
    }
    flags |= O_DIRECT;  /* direct access without buffering */
    flags |= O_NOATIME; /* disable updating 'atime' (access time) attribute on the file */
    
    err = fcntl(fd, F_SETFL, flags);
    if (err {
        fprintf(stderr, "[-] fcntl(O_DIRECT | O_NOATIME): %d: %s\n", errno, strerror(errno));
        return -1;
    }
    return 0;
#else
#error "unknown platform: can't set non-buffering"
    return -1;
#endif

}

/****************************************************************************
 ****************************************************************************/ 
int
my_random_reads(int fd, struct config *cfg, struct timings *t)
{
    size_t queue_depth = cfg->queue_depth;
    size_t i;
    int err;
    struct aiocb * *aiolist;
    struct mycontrolblock *mylist;
    struct stat st;

    /* Disable caching */
    util_file_disable_caching(fd);

    /* Alloc both a lit of AIO structures, plus a matching
     * list of our own structures */
    aiolist = calloc(queue_depth, sizeof(*aiolist));
    mylist = calloc(queue_depth, sizeof(*mylist));

    /* Discover the size of the file */
    err = fstat(fd, &st);
    if (err) {
        perror(cfg->filename);
        return -1;
    }
    if (st.st_size != cfg->filesize) {
        fprintf(stderr, "[-] filesize mismatch\n");
        exit(1);
    }
    

    /* Queue up the initial reads */
    for (i=0; i<queue_depth; i++) {
        struct aiocb *a;

        /* Create the I/O control block */
        a = calloc(1, sizeof(*a));
        aiolist[i] = a;
        a->aio_buf = malloc(cfg->read_length);

        /* queue the initial read */
        mycb_read(aiolist[i], &mylist[i], cfg->filesize, fd, cfg->read_length);
    }

    /* Now sit in a dispatch loop. This loop will end once we've reached
     * the maximum number of I/Os, the maximum amount of time, or
     * when an error occurs. */
    unsigned long long io_count = 0;
    time_t start = time(0);
    for (;;) {
        /* poll for asychronous events */
        err = aio_suspend((const struct aiocb *const *)aiolist, queue_depth, &cfg->dispatch_timeout);
        if (err < 0 && (errno == EAGAIN || errno == EINTR))
            continue; /* timeout expired */
        if (err < 0) {
            perror("aio_suspend");
            break;
        }

        /* Test each one */
        for (i=0; i<queue_depth; i++) {

            /* You must first use 'aio_error()' to test for EINPROGRESS
             * before testing return value */
            err = aio_error(aiolist[i]);
            if (err < 0) {
                perror("aio_error");
                exit(1); /* programming error */
            }

            /* The expected condition is EINPROGRESS if something
             * hasn't triggered */
            if (err == EINPROGRESS) {
                /* This item is still being worked on, so ignore it */
                //printf("-"); fflush(stdout);
                continue;
            } else {
                //printf("+"); fflush(stdout);
            }
                

            /* If non-zero, then some other error has occurred. This
             * shouldn't happen. */
            if (err != 0) {
                fprintf(stderr, "[-] asynchronous error: %s\n", strerror(err));
                                                                /*       ^^^ */
                break;
            }

            /* Now remove the event from the queue. */
            ssize_t count;
            count = aio_return(aiolist[i]);
            mycb_read_done(&mylist[i], aiolist[i]->aio_offset, (void*)aiolist[i]->aio_buf, count);
            timings_record(t, cfg->resolution, mylist[i].done - mylist[i].start);
            io_count++;

            /* Now reset the event */
            mycb_read(aiolist[i], &mylist[i], cfg->filesize, fd, cfg->read_length);
        }

        /* Quit after so many I/Os */
        if (io_count >= cfg->max_io_count)
            break;
        if (time(0) - start >= cfg->max_io_time)
            break;
    }

    if (fd > 0)
        close(fd);
    return 0;
}



/****************************************************************************
 * Creates a file and files it with junk that we can use for testing.
 ****************************************************************************/ 
static int
my_create_testfile(const char *filename, off_t filesize)
{
    int fd;
    off_t i;

    /* Create the test file to use. If no filename was specified, then
     * create a temp file. */
    if (filename) {
        fd = open(filename, O_RDWR|O_CREAT, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            fprintf(stderr, "[-] open(%s): %d: %s\n", filename, errno, strerror(errno));
            exit(1);
        } else {
            fprintf(stderr, "[+] created tmp file: %s\n", filename);
        }
    } else {
        FILE *fp = tmpfile();
        if (fp == NULL) {
            perror("tmpfile()");
            exit(1);
        }
        fd = fileno(fp);
        fprintf(stderr, "[+] created tmp file\n");
    }

    /* Disable caching */
    util_file_disable_caching(fd);

    /* Get the size of the existing file */
    int err;
    struct stat st;
    err = fstat(fd, &st);
    if (err) {
        perror("stat");
        return -1;
    }
    if (st.st_size >= filesize) {
        /* File is big enough, so just return without filling it */
        fprintf(stderr, "[+] file is big enough (%llu bytes)\n", st.st_size);
        return fd;
    }


    /* Create a large buffer, where every byte represents the 
     * the lowest 8 bits of the offset for that bytes */
    enum {BUF_SIZE = 4 * 1024 * 1024};
    char *buf = malloc(BUF_SIZE);
    for (i=0; i<BUF_SIZE; i++)
        buf[i] = (unsigned char)i;
    
    /* Fill the file with repeated copies of this buffer  */
    unsigned long long total_written = 0;
    for (i=0; (i + 1) * BUF_SIZE < filesize; i++) {
        ssize_t count;
        count = write(fd, buf, BUF_SIZE);
        if (count < 0) {
            perror(filename);
            exit(1);
        }
        total_written += count;
    }

    /* Fill the remaining bytes of the file, which will likely
     * be fraction of our buffer size */
    ssize_t count = write(fd, buf, filesize - total_written);
    if (count < 0) {
        perror(filename);
        exit(1);
    }

    total_written += count;
    fprintf(stderr, "[+] file size = %llu bytes\n", total_written);
    return fd;
}


/****************************************************************************
 ****************************************************************************/ 
static int 
simple_test(void)
{
    enum {QUEUE_DEPTH   = 200};
	struct aiocb   aio[QUEUE_DEPTH]; 
	struct aiocb  *aio_list[QUEUE_DEPTH] = {&aio[0], &aio[1]};
    const char *strs[] = {"encroach", "superb", "behold", "butter", "lizards"}; 
	int err;
    int fd;
    size_t i;

    /* Open any file recently, or maybe we could create a pipe. The
     * /dev/null is something that'll work everywhere but windows,
     * unless we are containerized */
    fd = open("/dev/null", O_RDWR);
    if (fd == -1) {
        perror("/dev/null");
        exit(1);
    }

    /* Setup the queues */
    off_t offset = 0;
    for (i=0; i<QUEUE_DEPTH; i++) {
        const char *buf = strs[i % (sizeof(strs)/sizeof(*strs))];
        struct aiocb *a = &aio[i];
        memset(a, 0, sizeof(*a));
        a->aio_fildes = fd;
        a->aio_buf = (volatile void *)buf;
        a->aio_nbytes = strlen(buf);
        a->aio_offset = offset;
        offset += strlen(buf);

        /* Now queue the write */
        err = aio_write(a);
        if (err == -1) {
            fprintf(stderr, "[-] aio_write[%u]: %d: %s\n", (unsigned)i, errno, strerror(errno));
            return errno;
        }
        
        /* Finally, add to our list that we'll check for
         * suspend */
        aio_list[i] = a;
    }
 
    /* Wait for results */ 
    for (;;) {
        struct timespec ts = {0, 1000ULL};
        err = aio_suspend((const struct aiocb *const *)aio_list, QUEUE_DEPTH, &ts);
        if (err == -1 && (errno == EINTR || errno == EAGAIN)) {
            /* Timeout occurred */
            continue;
        } else if (err == -1) {
            perror("aio_suspend");
            return errno; 
        } else if (err == 0) {
            break;
        }
    }

    /* Check for errors */
    for (i=0; i<QUEUE_DEPTH; i++) {
        err = aio_error(aio_list[i]);
        if (err == 0)
            continue;
        fprintf(stderr, "[+] aio_error=%d\n", err);
    }

    close(fd);
    return 0; 
}


/****************************************************************************
 ****************************************************************************/ 
int 
main(int argc, char *argv[])
{
    struct config cfg = {0};
    int fd;
    struct timings *t;

    assert(sizeof(off_t) >= 8);

    simple_test();


    t = calloc(1, sizeof(*t));

    /* Set some defaults for running this program */
    cfg.filename = 0;
    cfg.filesize = 100000000;
    cfg.queue_depth = 4;    /* four simultaneous outstanding operations */
    cfg.read_length = 64;   /* read 64 bytes at a time */
    cfg.alignment = 1; /* no alignment */
    cfg.dispatch_timeout.tv_sec = 0;
    cfg.dispatch_timeout.tv_nsec = 1000*1000*10; /* 10 milliseconds to wait for events */
    cfg.max_io_count = 1000000; /* no more than 1-million I/Os */
    cfg.max_io_time = 10; /* run for 10 seconds before exiting */
    cfg.resolution = 10000ULL; /* 10-microsecond resolution */

    /* Parse options from the command-line */
    cfg_parse_command_line(&cfg, argc, argv);
    
    /* Create the test file that we'll be using to read from */
    fd = my_create_testfile(cfg.filename, cfg.filesize);
    if (fd == -1)
        return -1;

    /* Now do a bunch of random reads */
    my_random_reads(fd, &cfg, t);

    size_t i;
    for (i=0; i<=100; i++)
        printf("%u ", t->buckets[i]);
    printf("\n");
    return 0;
}
