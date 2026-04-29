#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_PATH "/dev/usblogger"

void read_logs() {
    int fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open the device");
        return;
    }

    char buffer[8192]; // Large enough for multiple events
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read >= 0) {
        buffer[bytes_read] = '\0';
        printf("--- USB Activity Logs ---\n%s\n", buffer);
    } else {
        perror("Failed to read from device");
    }
    close(fd);
}

void clear_logs() {
    int fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open the device");
        return;
    }

    if (write(fd, "clear", 5) < 0) {
        perror("Failed to clear logs");
    } else {
        printf("Logs cleared successfully.\n");
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s [read | clear]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "read") == 0) {
        read_logs();
    } else if (strcmp(argv[1], "clear") == 0) {
        clear_logs();
    } else {
        printf("Invalid option.\n");
    }

    return 0;
}
