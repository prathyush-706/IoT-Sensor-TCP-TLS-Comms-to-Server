// Prathyush Sivakumar
// prathyush1999@ucla.edu
// 704908810

#include <errno.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/time.h>
#include <stdbool.h>
#include <getopt.h>
#include <mraa.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <openssl/ssl.h>
#include <ctype.h>

#define A0 1
#define GPIO_50 60
#define B 4275
#define R0 100000.0

sig_atomic_t volatile run_flag = 1;
int period = 1;
char scale = 'F';
int log_file = -1;
mraa_gpio_context button;
mraa_aio_context thermistor;
struct timespec ts;
struct tm *tm;
struct timespec prev_time;
bool first = true;
bool print_reports = true;
char* remaining;
ssize_t remaining_len = 0;

//New additions since lab4b
char* uid = NULL;
char* host = NULL;
struct hostent* server;
struct sockaddr_in server_addr;
int port = -1;
int sockfd = -1;
SSL_CTX* newContext = NULL;
SSL *sslClient = NULL;

float
get_temp(int sensor_value)
{
    float R = 1023.0/((float) sensor_value) - 1.0;
    R = R0 * R;
    //C is the temperature in Celcious
    float C = 1.0/(log10(R/R0)/B + 1/298.15) - 273.15;
    //F is the temperature in Fahrenheit
    float F = (C * 9)/5 + 32;

    if (scale == 'F')
        return F;
    else
        return C;
}

void
format_report(char* buf, int sensor_value)
{
    clock_gettime(CLOCK_REALTIME, &ts);
    
    tm = localtime(&(ts.tv_sec));
    float temp = get_temp(sensor_value);
    sprintf(buf, "%02d:%02d:%02d %.1f\n", tm->tm_hour, tm->tm_min, tm->tm_sec, temp);
    if (first || (ts.tv_sec * 1000000000L + ts.tv_nsec - (prev_time.tv_sec * 1000000000L + prev_time.tv_nsec) >= period * 1000000000L && run_flag && print_reports))
    {
        first = false;
        write(log_file, buf, 14);
        SSL_write(sslClient, buf, 14);
        clock_gettime(CLOCK_REALTIME, &prev_time);
    }
}

void
do_when_interrupted()
{
    run_flag = 0;
    char buffer[18];
    sprintf(buffer, "%02d:%02d:%02d SHUTDOWN\n", tm->tm_hour, tm->tm_min, tm->tm_sec);
    write(log_file, buffer, 18);
    SSL_write(sslClient, buffer, 18);
}

int get_index(char* string, char c) 
{
    char *e = strchr(string, c);
    if (e == NULL) {
        return -1;
    }
    return (int)(e - string);
}

void
cmd_process()
{
    char buff_temp[remaining_len + 1025];
    memcpy(buff_temp, remaining, remaining_len);
    char *mybuf = buff_temp;
    int i = remaining_len;
    for (; i< remaining_len + 1025; i++) 
    {
        mybuf[i] = '\0';
    }
    int mybuf_len = SSL_read(sslClient, mybuf+remaining_len, 1024);
    if (mybuf_len == -1)
    {
        fprintf(stderr, "failed to read from socket: %s\n", strerror(errno));
        exit(1);
    }
    int max = remaining_len + mybuf_len;
    for (i = 0; i < max;)
    {
        if (mybuf[i] == '\n')
        {
            i++;
            continue;
        }
        if (mybuf[i] == '\0')
        {
            break;
        }
        int newline_index = get_index(mybuf + i, '\n');
        if (!strncmp(mybuf + i, "SCALE=F\n", 8))
        {
            scale = 'F';
        }
        else if (!strncmp(mybuf + i, "SCALE=C\n", 8))
        {
            scale = 'C';
        }
        else if (!strncmp(mybuf + i, "PERIOD=", 7))
        {
            char period_mybuf[4] = {'\0','\0', '\0', '\0'};
            int j = 7;
            for (; j < 10; j++) 
            {
                if ((mybuf+i)[j] == '\n') 
                {
                    break;
                }
                period_mybuf[j-7] = (mybuf+i)[j];
            }
            period = atoi(period_mybuf);
            if (period <= 0)
            {
                fprintf(stderr, "Invalid period\n");
                exit(1);
            }
        }
        else if (!strncmp(mybuf + i, "STOP\n", 5))
        {
            print_reports = false;
        }
        else if (!strncmp(mybuf + i, "START\n", 6))
        {
            print_reports = true;
        }
        else if (!strncmp(mybuf + i, "OFF\n", 4))
        {
            do_when_interrupted();
        }
        if (newline_index == -1)
        {
            size_t initial = remaining_len;
            remaining_len += max - (i + initial);
            remaining = (char *)realloc(remaining, remaining_len);
            if (remaining == NULL) 
            {
                fprintf(stderr, "Call to realloc failed\n");
                exit(1);
            }
            memcpy(remaining + initial, mybuf + initial + i, max - (i + initial));
            break;
        }
        if (remaining_len) 
        {
            remaining_len = 0;
            free(remaining);
            remaining = (char *)malloc(0);
            if (remaining == NULL) 
            {
                exit(1);
            }
        }

        if(write(log_file, mybuf + i, newline_index + 1) == -1)
        {
            fprintf(stderr, "Could not write to log file\n");
            exit(1);
        }

        SSL_write(sslClient, mybuf + i, newline_index + 1);

        i += newline_index + 1;
    }
}

int
main(int argc, char **argv)
{   
    static struct option prog_options[] =
    {
      {"period", required_argument, NULL, 'p'},
      {"scale", required_argument, NULL, 's'},
      {"log", required_argument, NULL, 'l'},
      {"id", required_argument, NULL, 'i'},
      {"host", required_argument, NULL, 'h'},
      {0, 0, 0, 0}
    };


    prev_time.tv_sec = 0;
    prev_time.tv_nsec = 0;
    int option_index = 0;
    while(1)
    {
        int ch = getopt_long(argc, argv, "", prog_options, &option_index);
        if (ch == -1)
            break;
        switch(ch)
        {
            case 'p':
            if(optarg)
            {
                period = atoi(optarg);
                if (period <= 0)
                {
                    fprintf(stderr, "Bad argument to period option\n");
                    exit(1);
                }
            }
            break;

            case 's':
            if (optarg)
            {
                scale = optarg[0];
                if (scale != 'F' && scale != 'C')
                {
                    fprintf(stderr, "Bad argument to scale option\n");
                    exit(1);
                }
            }
            break;

            case 'l':
            if (optarg)
            {
                log_file = open(optarg, O_CREAT | O_WRONLY | O_TRUNC);
                if (log_file == -1)
                {
                    fprintf(stderr, "Log file could not be created or opened\n");
                    exit(1);
                }
            }
            break;

            case 'i':
            uid = optarg;
            break;

            case 'h':
            host = optarg;
            break;

            default:
            fprintf(stderr, "Unrecognized program option\n");
            exit(1);

        }
    }

    if (optind < argc)
    {
        port = atoi(argv[optind]);
        if (port <= 0)
        {
            fprintf(stderr, "Invalid port number.\n");
            exit(1);
        }
    }

    if (strlen(uid) != 9 || host == NULL || log_file == -1)
    {
        fprintf(stderr, "ID, host and log options are mandatory.\n");
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
      fprintf(stderr, "%s", strerror(errno));
      exit(1);
    }

    memset( (void *) &server_addr, 0, sizeof(server_addr));
    server_addr.sin_port = htons(port);
    server_addr.sin_family = AF_INET;
    server = gethostbyname(host);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if(connect(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1)
    {
      fprintf(stderr, "%s\r\n", strerror(errno));
      exit(1);
    }

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    newContext = SSL_CTX_new(TLSv1_client_method());
    if (!newContext)
    {
        fprintf(stderr, "Failed to create new context for SSL\n");
        exit(1);
    }
    sslClient = SSL_new(newContext);
    if (!sslClient)
    {
        fprintf(stderr, "Failed to setup SSL connection\n");
        exit(1);
    }
    if (!(SSL_set_fd(sslClient, sockfd)))
    {
        fprintf(stderr, "Failed to set file descriptor for SSL\n");
        exit(1);
    }
    if (SSL_connect(sslClient) != 1)
    {
        fprintf(stderr, "Failed to initiate SSL connection\n");
        exit(1);
    }
    char s_imm[20];
    sprintf(s_imm, "ID=%s\n", uid);
    write(log_file, s_imm, sizeof(s_imm));
    SSL_write(sslClient, s_imm, sizeof(s_imm));
    if (server == NULL)
    {
        fprintf(stderr, "Failed to get the server name\n");
        exit(1);
    }

    button = mraa_gpio_init(GPIO_50);
    if (mraa_gpio_dir(button, MRAA_GPIO_IN) != MRAA_SUCCESS)
    {
        fprintf(stderr, "Could not set button GPIO pin to input\n");
        mraa_deinit();
        exit(1);
    }
    mraa_gpio_isr(button, MRAA_GPIO_EDGE_RISING, &do_when_interrupted, NULL);
    if (button == NULL)
    {
        fprintf(stderr, "Failed to initialize GPIO pin 50\n");
	    mraa_deinit();
        exit(1);
    }
    mraa_gpio_dir(button, MRAA_GPIO_IN);
    
    thermistor = mraa_aio_init(A0);
    if (thermistor == NULL)
    {
        fprintf(stderr, "Failed to initialize Analog Pin A0\n");
	    mraa_deinit();
        exit(1);
    }

    u_int16_t value;

    struct pollfd fd_events[1] =
    {
        {sockfd, POLLIN, 0}
    };

    char buf[256];

    while(run_flag)
    {
        value = mraa_aio_read(thermistor);
        format_report(buf, value);
        int ret = poll(fd_events, 1, 50);
        if (ret && fd_events[0].revents & POLLIN)
        {
            cmd_process();
        }

    }
    mraa_aio_close(thermistor);
    mraa_gpio_close(button);
    if (log_file != -1)
    {
        close(log_file);
    }
    close(sockfd);
    exit(0);
}

