#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define IN 0
#define OUT 1
#define PWM 2

#define LOW 0
#define HIGH 1
#define VALUE_MAX 256
#define PIN 20
#define POUT 21

int readState = 0;
char three[2] = "3";

int serv_sock, clnt_sock = -1;
struct sockaddr_in serv_addr, clnt_addr;
socklen_t clnt_addr_size;
char msg[2];

static int
PWMExport(int pwmnum)
{
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    int bytes_written;
    int fd;

    fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in export!\n");
        return (-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
    write(fd, buffer, bytes_written);
    close(fd);

    sleep(1);
    fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in export!\n");
        return (-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
    write(fd, buffer, bytes_written);
    close(fd);
    sleep(1);
    return (0);
}

static int
PWMEnable(int pwmnum)
{
    static const char s_unenable_str[] = "0";
    static const char s_enable_str[] = "1";

#define DIRECTION_MAX 45
    char path[DIRECTION_MAX];
    int fd;
    snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in enable!\n");
        return -1;
    }

    write(fd, s_unenable_str, strlen(s_unenable_str));
    close(fd);

    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in enable!\n");
        return -1;
    }

    write(fd, s_enable_str, strlen(s_enable_str));
    close(fd);
    return (0);
}

static int
PWMWritePeriod(int pwmnum, int value)
{
    char s_values_str[VALUE_MAX];
    char path[VALUE_MAX];
    int fd, byte;

    snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/period", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in period\n");
        return (-1);
    }

    byte = snprintf(s_values_str, 10, "%d", value);
    if (-1 == write(fd, s_values_str, byte))
    {
        fprintf(stderr, "Failed to write value in period!'\n");
        close(fd);
        return (-1);
    }

    close(fd);
    return (0);
}

static int
PWMWriteDutyCycle(int pwmnum, int value)
{
    char path[VALUE_MAX];
    char s_values_str[VALUE_MAX];
    int fd, byte;

    snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/duty_cycle", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in duty_cycle!\n");
        return (-1);
    }

    byte = snprintf(s_values_str, 10, "%d", value);

    if (-1 == write(fd, s_values_str, byte))
    {
        fprintf(stderr, "Failed to write calue! in duty_cycle\n");
        return (-1);
    }

    close(fd);
    return (0);
}

void *led_thd()
{
    while (1)
    {
        if (readState == 1)
        {

            for (int i = 0; i < 1000; i++)
            {
                PWMWriteDutyCycle(0, i * 10000);
                usleep(1000);
            }
            for (int i = 1000; i > 0; i--)
            {
                PWMWriteDutyCycle(0, i * 10000);
                usleep(1000);
            }
        }
        else
        {
            PWMWriteDutyCycle(0, 0);
        }
    }
}

void *speaker_thd()
{
    while (1)
    {
        if (readState == 1)
        {

            for (int i = 0; i < 1000; i++)
            {
                PWMWriteDutyCycle(1, i * 10000);
                usleep(1000);
            }
            for (int i = 1000; i > 0; i--)
            {
                PWMWriteDutyCycle(1, i * 10000);
                usleep(1000);
            }
        }
        else
        {
            PWMWriteDutyCycle(1, 0);
        }
    }
}

void *socket_thd()
{
    while (1)
    {
        read(clnt_sock, msg, 2);
        if (strncmp(three, msg, 1) == 0)
        {
            printf("alarm start\n");
            readState = 1;
            usleep(10000000);
            readState = 0;
        }
    }
}
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

int main(int argc, char *argv[])
{

    PWMExport(0); // pwm0 is gpio18
    PWMWritePeriod(0, 20000000);
    PWMWriteDutyCycle(0, 0);
    PWMEnable(0);
    PWMExport(1); // pwm1 is gpio19
    PWMWritePeriod(1, 10000000);
    PWMWriteDutyCycle(1, 0);
    PWMEnable(1);

    if (argc != 2)
    {
        printf("Usage : %s <port>\n", argv[0]);
    }
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
        error_handling("socket() error");
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));
    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");
    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    if (clnt_sock < 0)
    {
        clnt_addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);
        if (clnt_sock == -1)
            error_handling("accept() error");
    }

    pthread_t p_thread[2];
    int thr_id;
    int status;
    char p1[] = "thread_1";
    char p2[] = "thread_2";
    char p3[] = "thread_3";
    char pM[] = "thread_m";
    thr_id = pthread_create(&p_thread[0], NULL, speaker_thd, (void *)p1);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[1], NULL, led_thd, (void *)p2);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[2], NULL, socket_thd, (void *)p3);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }

    pthread_join(p_thread[0], (void **)&status);
    pthread_join(p_thread[1], (void **)&status);
    pthread_join(p_thread[2], (void **)&status);
    printf("thread finish\n");
    PWMWriteDutyCycle(0, 0);
    PWMWriteDutyCycle(1, 0);
}