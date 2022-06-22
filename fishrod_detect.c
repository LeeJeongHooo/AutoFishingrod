#include <sys/stat.h>
#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <linux/spi/spidev.h>
#include <getopt.h>
#include <pthread.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_MAX 3
#define DIRECTION_MAX 45
#define VALUE_MAX 256

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define POUT 23 // Ultra
#define PIN 24  // Ultra
#define PINV 20 // Tilt

#define ARRAY_SIZE(array) sizeof(array) / sizeof(array[0])
static const char *DEVICE = "/dev/spidev0.0";
static uint8_t MODE = SPI_MODE_0;
static uint8_t BITS = 8;
static uint32_t CLOCK = 1000000;
static uint16_t DELAY = 5;

static int GPIOExport(int pin)
{

    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open export for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIOUnexport(int pin)
{
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return (-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIODirection(int pin, int dir)
{
    static const char s_directions_str[] = "in\0out";
    char path[DIRECTION_MAX] = "/sys/class/gpio/gpio%d/direction";
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);

    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return (-1);
    }

    if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3))
    {
        fprintf(stderr, "Failed to set direction!\n");
        return (-1);
    }

    close(fd);
    return (0);
}

static int GPIORead(int pin)
{
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return (-1);
    }

    if (-1 == read(fd, value_str, 3))
    {
        fprintf(stderr, "Failed to read value!\n");
        return (-1);
    }

    close(fd);

    return (atoi(value_str));
}

static int GPIOWrite(int pin, int value)
{
    static const char s_values_str[] = "01";
    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return (-1);
    }

    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1))
    {
        fprintf(stderr, "Failed to write value!\n");
        return (-1);

        close(fd);
        return (0);
    }
}

// ADC
static int prepare(int fd)
{

    if (ioctl(fd, SPI_IOC_WR_MODE, &MODE) == -1)
    {
        perror("Can't set MODE");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &BITS) == -1)
    {
        perror("Can't set number of BITS");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &CLOCK) == -1)
    {
        perror("Can't set write CLOCK");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &CLOCK) == -1)
    {
        perror("Can't set read CLOCK");
        return -1;
    }

    return 0;
}
/*******************************************************************************************************************/

uint8_t control_bits_differential(uint8_t channel)
{
    return (channel & 7) << 4;
}

uint8_t control_bits(uint8_t channel)
{
    return 0x8 | control_bits_differential(channel);
}

int readadc(int fd, uint8_t channel)
{
    uint8_t tx[] = {1, control_bits(channel), 0};
    uint8_t rx[3];

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = ARRAY_SIZE(tx),
        .delay_usecs = DELAY,
        .speed_hz = CLOCK,
        .bits_per_word = BITS,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) == 1)
    {
        perror("IO Error");
        abort();
    }

    return ((rx[1] << 8) & 0x300) | (rx[2] & 0xFF);
}

/*------------------------------------------------------------------------------------------------------------------*/
int tilt_check = 0;
int waterlevel = 0;
int waterlevelcheck = 0;
double distance;
int distancecheck = 0;
int state = 0; // all noting excute

int sock;
struct sockaddr_in serv_addr;
char start[2] = "1";
char end[2] = "9";

void *ultrawave_thd()
{
    clock_t start_t, end_t;
    double time;

    // Enable GPIO pins
    if (-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN))
    {
        printf("gpio export err\n");
        exit(0);
    }
    // wait for writing to export file
    usleep(100000);

    // Set GPIO directions
    if (-1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(PIN, IN))
    {
        printf("gpio direction err\n");
        exit(0);
    }

    // init ultrawave trigger
    GPIOWrite(POUT, 0);
    usleep(10000);
    // start
    while (1)
    {
        if (state == 1)
        {
            if (-1 == GPIOWrite(POUT, 1))
            {
                printf("gpio write/trigger err\n");
                exit(0);
            }

            // 1sec == 1000000ultra_sec, 1ms = 1000ultra_sec
            usleep(10);
            GPIOWrite(POUT, 0);

            while (GPIORead(PIN) == 0)
            {
                start_t = clock();
            }
            while (GPIORead(PIN) == 1)
            {
                end_t = clock();
            }

            time = (double)(end_t - start_t) / CLOCKS_PER_SEC; // ms
            distance = time / 2 * 34000;
            if (distance > 900)
                distance = 900;

            if (distance < 10)
            {
                distancecheck = 1;
                printf("distance check SUCCESS!!!!!\n");
            }
            else
            {
                distancecheck = 0;
                printf("distance check Fail!!!!!\n");
            }

            usleep(500000);
        }
    }
}

void *waterlevel_thd()
{

    int fd = open(DEVICE, O_RDWR);
    if (fd <= 0)
    {
        printf("Device %s not found\n", DEVICE);
        return -1;
    }

    if (prepare(fd) == -1)
    {
        return -1;
    }

    while (1)
    {
        if (state == 1)
        {
            waterlevel = readadc(fd, 0);
            if (waterlevel > 180)
            {
                printf("waterlevelcheck success\n");
                waterlevelcheck = 1;
            }
            usleep(500000);
        }
    }

    close(fd);
}

void *tilt_thd()
{
    int tilt_state = 1;

    while (1)
    {
        if (state == 1)
        {
            if (-1 == GPIOExport(PINV))
                return 0;

            if (GPIODirection(PINV, IN))
                return 0;

            usleep(3000);

            tilt_state = GPIORead(PINV);
            if (tilt_state == 0)
            { // no tilt
                printf("No tilt\n");
                tilt_check = 0;
            }
            else
            {
                printf("tilt_check success\n");
                tilt_check = 1;
            }
            usleep(500000);
        }
    }
}

void *socket_read_thd()
{

    while (1)
    {

        char msg[2];
        read(sock, msg, sizeof(msg));

        if (strncmp(start, msg, 1) == 0)
        {
            printf("Start detect signal from motorserver!!!!\n");
            tilt_check = 0;
            waterlevelcheck = 0;
            distancecheck = 0;
            state = 1;
        }
        if (strncmp(end, msg, 1) == 0)
        {
            printf("End signal from motorserver!!!!\n");
            state = 0;
            tilt_check = 0;
            waterlevelcheck = 0;
            distancecheck = 0;
        }
    }
}

void *socket_write_thd()
{
    while (1)
    {
        while (waterlevelcheck && distancecheck && tilt_check)
        {

            char msg[2];
            printf("All sensor detect!!!!!!!!\n");
            snprintf(msg, 2, "%d", 2);
            write(sock, msg, sizeof(msg));
            printf("Send message to MotorServer : %s\n", msg);
            state = 0;
            waterlevelcheck = 0;
            distancecheck = 0;
            tilt_check = 0;
            break;
            ;
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
    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    pthread_t p_thread[5];
    int thr_id;
    int status;
    char p1[] = "thread_1";
    char p2[] = "thread_2";
    char p3[] = "thread_3";
    char p4[] = "thread_4";
    char p5[] = "thread_5";

    thr_id = pthread_create(&p_thread[0], NULL, ultrawave_thd, (void *)p1);
    if (thr_id < 0)
    {
        perror("thread create error: ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[1], NULL, waterlevel_thd, (void *)p2);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[2], NULL, tilt_thd, (void *)p3);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[3], NULL, socket_write_thd, (void *)p4);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[4], NULL, socket_read_thd, (void *)p5);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }

    pthread_join(p_thread[0], (void **)&status);
    pthread_join(p_thread[1], (void **)&status);
    pthread_join(p_thread[2], (void **)&status);
    pthread_join(p_thread[3], (void **)&status);
    pthread_join(p_thread[4], (void **)&status);

    close(sock);
}