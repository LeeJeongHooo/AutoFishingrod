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
#define PIN 20         // botton1
#define PIN2 18        // magnetic
#define POUT 17        // moter drive in1
#define POUT2 23       // led
#define POUT3 27       // moter drive in2
#define port1 8080     // detectpi port
#define IP 192.168.0.9 // alarm PI add
#define port2 8088     // alarm PI port

#define ARRAY_SIZE(array) sizeof(array) / sizeof(array[0])
static const char *DEVICE = "/dev/spidev0.0";
static uint8_t MODE = SPI_MODE_0;
static uint8_t BITS = 8;
static uint32_t CLOCK = 1000000;
static uint16_t DELAY = 5;

int mag = 0;
int state = 0;

char argv1[20], argv2[20];

// server
int serv_sock1, clnt_sock1 = -1;
struct sockaddr_in serv_addr1, clnt_addr1;
socklen_t clnt_addr_size1;

// client
char two[2] = "2";

int str_len;
int moterOn = 0;
int send_signal = 0;
int *clie_send_thd()
{

    int sock;
    struct sockaddr_in serv_addr2;
    int str_len2;
    char msg2[2];

    printf("signal ready\n");

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr2, 0, sizeof(serv_addr2));
    serv_addr2.sin_family = AF_INET;
    serv_addr2.sin_addr.s_addr = inet_addr(argv1);
    serv_addr2.sin_port = htons(atoi(argv2));

    printf("serv_addr2 saved\n");
    printf("%s\n", argv1);
    printf("%s\n", argv2);

    if (connect(sock, (struct sockaddr *)&serv_addr2, sizeof(serv_addr2)) == -1)
        error_handling("connect() error");

    printf("connected to alarm device\n");

    while (1)
    {
        if (send_signal == 1)
        {
            snprintf(msg2, 2, "%d", 3);

            write(sock, msg2, sizeof(msg2));

            printf("send 3\n");

            usleep(100000);

            printf("send finish\n");
            send_signal = 0;
        }
    }
    close(sock);

    printf("close finish\n");

    return 0;
}

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
        fprintf(stderr, "Failed to open gpio value for reading at value %d!\n", pin);
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
        fprintf(stderr, "Failed to open gpio value for writing at %d!\n", pin);
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
int magnetic = 1;
void *magnetic_thd()
{

    while (1)
    {

        magnetic = GPIORead(PIN2); // when detects magnetic, magnetic'value is 0

        if (magnetic == 0)
        { // detected
            moterOn = 0;
            printf("moter stop!\n");
            GPIOWrite(POUT, 1);
            GPIOWrite(POUT3, 1);
            GPIOWrite(POUT2, 0);
        }

        usleep(100000);
    }
}

int right = 1;
int left = 0;

void turnRight()
{
    right = 1;
    left = 0;
    printf("turn right\n");
}

void turnLeft()
{
    right = 0;
    left = 1;
    printf("turn left\n");
}

void *moter_thd()
{

    while (1)
    {
        if (moterOn == 1)
        {

            GPIOWrite(POUT, right);
            GPIOWrite(POUT3, left);
            GPIOWrite(POUT2, 1); // led On
        }

        usleep(150000);
    }
}
int button = 0;

void *button_thd()
{
    int prev_state = 1;

    if (-1 == GPIOWrite(POUT2, 0))
        return (3);
    while (1)
    {

        state = GPIORead(PIN);

        if (prev_state == 0 && state == 1)
        {
            printf("changed\n");
            button = 1 - button;
        }

        prev_state = state;

        usleep(160000);
    }
}
int detectState = 0;
void *server_send_thd()
{

    while (1)
    {
        char msg1[2];
        if (button == 1)
        {

            if (detectState == 1)
            {
                printf("send 9\n");
                snprintf(msg1, 2, "%d", 9);
                write(clnt_sock1, msg1, sizeof(msg1));
                detectState = 0;
                button = 0;

                moterOn = 1;
                turnLeft();
                continue;
            }

            printf("send 1\n");
            snprintf(msg1, 2, "%d", 1);
            write(clnt_sock1, msg1, sizeof(msg1));
            detectState = 1;
            button = 0;
        }

        usleep(200000);
    }
}
void *server_read_thd()
{

    while (1)
    {
        char msg1[2];
        printf("waiting value\n");
        str_len = read(clnt_sock1, msg1, sizeof(msg1));
        if (str_len == -1)
            error_handling("read() error");

        if (strncmp(two, msg1, 1) == 0)
        {
            printf("Receive message from Server : %s\n", msg1);
            detectState = 0;
            printf("moter is running\n");
            turnRight();
            moterOn = 1;
            usleep(1000000);
            turnLeft();
            send_signal = 1;
            printf("send signal to Alarm Device\n");
        }

        usleep(220000);
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
    serv_sock1 = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock1 == -1)
        error_handling("socket() error");
    memset(&serv_addr1, 0, sizeof(serv_addr1));
    serv_addr1.sin_family = AF_INET;
    serv_addr1.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr1.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock1, (struct sockaddr1 *)&serv_addr1, sizeof(serv_addr1)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock1, 5) == -1)
        error_handling("listen() error");

    if (clnt_sock1 < 0)
    {
        clnt_addr_size1 = sizeof(clnt_addr1);
        clnt_sock1 = accept(serv_sock1, (struct sockaddr1 *)&clnt_addr1, &clnt_addr_size1);
        if (clnt_sock1 == -1)
            error_handling("accept() error");
    }

    for (int a = 0; a < strlen(argv[2]); a++)
    {

        argv1[a] = argv[2][a];
    }
    printf("%s\n", argv1);
    for (int b = 0; b < strlen(argv[3]); b++)
    {

        argv2[b] = argv[3][b];
    }

    // Enable GPIO pins
    if (-1 == GPIOExport(PIN) || -1 == GPIOExport(PIN2) || -1 == GPIOExport(POUT) || -1 == GPIOExport(POUT2) || -1 == GPIOExport(POUT3))
        return (1);
    // Set GPIO directions
    if (-1 == GPIODirection(PIN, IN) || -1 == GPIODirection(PIN2, IN) || -1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(POUT2, OUT) || -1 == GPIODirection(POUT3, OUT))
        return (2);

    while (1)
    {
        pthread_t p_thread[5];
        int thr_id;
        int status;
        char p1[] = "thread_1";
        char p2[] = "thread_2";
        char p3[] = "thread_3";
        char p4[] = "thread_4";
        char p5[] = "thread_5";
        char p6[] = "thread_6";

        thr_id = pthread_create(&p_thread[0], NULL, moter_thd, (void *)p1);
        if (thr_id < 0)
        {
            perror("thread create error: ");
            exit(0);
        }

        thr_id = pthread_create(&p_thread[1], NULL, magnetic_thd, (void *)p2);
        if (thr_id < 0)
        {
            perror("thread create error : ");
            exit(0);
        }

        thr_id = pthread_create(&p_thread[2], NULL, button_thd, (void *)p3);
        if (thr_id < 0)
        {
            perror("thread create error : ");
            exit(0);
        }
        thr_id = pthread_create(&p_thread[3], NULL, server_send_thd, (void *)p4);
        if (thr_id < 0)
        {
            perror("thread create error : ");
            exit(0);
        }
        thr_id = pthread_create(&p_thread[4], NULL, server_read_thd, (void *)p5);
        if (thr_id < 0)
        {
            perror("thread create error : ");
            exit(0);
        }
        thr_id = pthread_create(&p_thread[5], NULL, clie_send_thd, (void *)p6);
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
        pthread_join(p_thread[5], (void **)&status);

        close(clnt_sock1);
        close(serv_sock1);

        // Disable GPIO pins
        if (-1 == GPIOUnexport(PIN) || -1 == GPIOUnexport(PIN2) || -1 == GPIOUnexport(POUT) || -1 == GPIOUnexport(POUT2) || -1 == GPIOUnexport(POUT3))
            return (4);
    }
}
